#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <limits.h>
#include <filesystem>
#include <string>
#include <cstdlib>
#include <cstdint>

// ODBC
#include <sql.h>
#include <sqlext.h>

// helpers
static bool looks_ascii_strict(const char* buf, size_t n) {
    size_t ok = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = buf[i];
        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 126)) ok++;
    }
    return ok >= n * 0.98; // 98%+ printable ASCII
}

static void odbc_check(SQLRETURN rc, SQLHANDLE h, SQLSMALLINT type, const char* where) {
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) return;
    std::cerr << "ODBC error at " << where << ":\n";
    SQLSMALLINT i = 1; SQLCHAR state[6], msg[1024]; SQLINTEGER nat; SQLSMALLINT len;
    while (SQLGetDiagRec(type, h, i++, state, &nat, msg, sizeof(msg), &len) == SQL_SUCCESS) {
        std::cerr << "  [" << state << "] (" << nat << ") " << msg << "\n";
    }
    std::exit(1);
}

// big-endian helpers
static inline int be16(const unsigned char* b, int off) {
    return (b[off] << 8) | b[off+1];
}
static inline int32_t be32(const unsigned char* b, int off) {
    return ( (int32_t)b[off] << 24 ) | ( (int32_t)b[off+1] << 16 ) | ( (int32_t)b[off+2] << 8 ) | (int32_t)b[off+3];
}

// map SEG-Y data format code -> bytes per sample
// 1: 4-byte IBM float, 2: 4-byte int, 3: 2-byte int,
// 5: 4-byte IEEE float, 6: 8-byte IEEE float, 8: 1-byte int
static inline size_t bytes_per_sample_from_format(int fmt) {
    switch (fmt) {
        case 1: return 4;
        case 2: return 4;
        case 3: return 2;
        case 5: return 4;
        case 6: return 8;
        case 8: return 1;
        default: return 4; // reasonable default
    }
}

int main(int argc, char* argv[]) {
    // open file
    const char* path = (argc > 1) ? argv[1] : "data/1x1.sgy";

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        char cwd[PATH_MAX]; getcwd(cwd, sizeof(cwd));
        std::cerr << " Could not open: " << path << "\nCWD: " << cwd
                  << "\nExists? " << (std::filesystem::exists(path) ? "yes" : "no") << "\n";
        return 1;
    }

    // text header (3200)
    char textHeader[3200];
    file.read(textHeader, 3200);
    if (!file || file.gcount() != 3200) {
        std::cerr << "Short read on text header.\n";
        return 2;
    }
    if (!looks_ascii_strict(textHeader, 3200)) {
        std::cerr << "⛔ Rejected: text header is not ASCII (policy = ASCII only).\n";
        return 2;
    }

    std::cout << "=== Text Header (first 200) ===\n";
    std::cout.write(textHeader, 200);
    std::cout << "\n\n";

    // binary header (400)
    unsigned char bh[400];
    file.read(reinterpret_cast<char*>(bh), 400);
    if (!file || file.gcount() != 400) {
        std::cerr << "Short read on binary header.\n";
        return 3;
    }

    int sampleInterval = be16(bh, 16); // bytes 17–18 (1-based)
    int numSamples     = be16(bh, 20); // bytes 21–22
    int formatCode     = be16(bh, 24); // bytes 25–26

    std::cout << "=== Binary Header Info ===\n";
    std::cout << "Sample Interval (microseconds): " << sampleInterval << "\n";
    std::cout << "Samples per Trace: " << numSamples << "\n";
    std::cout << "Data Format Code: " << formatCode << "\n";

    // sanitize text header to ASCII (spaces for non-printables)
    std::string text_ascii(textHeader, textHeader + 3200);
    for (char &c : text_ascii) {
        unsigned char u = (unsigned char)c;
        if (!(u == 9 || u == 10 || u == 13 || (u >= 32 && u <= 126))) c = ' ';
    }

    // ---- ODBC connect ----
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;

    odbc_check(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env), env, SQL_HANDLE_ENV, "Alloc ENV");
    odbc_check(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0), env, SQL_HANDLE_ENV, "Set ODBC3");
    odbc_check(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc), dbc, SQL_HANDLE_DBC, "Alloc DBC");

    // Prefer DSN (easier to switch between local and RDS)
    const char* dsnEnv = std::getenv("SEISMICS_DSN");
    std::string connStr = dsnEnv ? std::string("DSN=") + dsnEnv + ";" : "DSN=seismics_dsn;";

    SQLCHAR out[1024]; SQLSMALLINT outlen = 0;
    odbc_check(SQLDriverConnect(dbc, NULL,
                                (SQLCHAR*)connStr.c_str(), SQL_NTS,
                                out, sizeof(out), &outlen, SQL_DRIVER_NOPROMPT),
               dbc, SQL_HANDLE_DBC, "DriverConnect");

    // start transaction
    odbc_check(SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0),
               dbc, SQL_HANDLE_DBC, "Set AUTOCOMMIT OFF");

    //insert/update files, get id
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    odbc_check(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), stmt, SQL_HANDLE_STMT, "Alloc STMT files");
    odbc_check(SQLPrepare(stmt, (SQLCHAR*)
        "INSERT INTO files (file_path, text_header) VALUES (?, ?) "
        "ON CONFLICT (file_path) DO UPDATE SET text_header=EXCLUDED.text_header "
        "RETURNING id;", SQL_NTS), stmt, SQL_HANDLE_STMT, "Prepare files");

    SQLLEN path_ind = SQL_NTS;
    SQLLEN text_ind = (SQLLEN)text_ascii.size();

    odbc_check(SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                                0, 0, (SQLPOINTER)path, 0, &path_ind),
               stmt, SQL_HANDLE_STMT, "Bind p1");
    odbc_check(SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_LONGVARCHAR,
                                0, 0, (SQLPOINTER)text_ascii.c_str(), 0, &text_ind),
               stmt, SQL_HANDLE_STMT, "Bind p2");
    odbc_check(SQLExecute(stmt), stmt, SQL_HANDLE_STMT, "Exec files");

    long long file_id = 0;
    odbc_check(SQLBindCol(stmt, 1, SQL_C_SBIGINT, &file_id, 0, NULL), stmt, SQL_HANDLE_STMT, "BindCol id");
    {
        SQLRETURN frc = SQLFetch(stmt);
        if (!(frc == SQL_SUCCESS || frc == SQL_SUCCESS_WITH_INFO)) {
            std::cerr << "Did not fetch file id.\n";
            SQLFreeStmt(stmt, SQL_CLOSE);
            SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_ROLLBACK);
            SQLDisconnect(dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            return 4;
        }
    }
    SQLFreeStmt(stmt, SQL_CLOSE);

    //  upsert binary_headers (1:1)
    odbc_check(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), stmt, SQL_HANDLE_STMT, "Alloc STMT bin");
    odbc_check(SQLPrepare(stmt, (SQLCHAR*)
        "INSERT INTO binary_headers (file_id, sample_interval_us, samples_per_trace, data_format_code) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT (file_id) DO UPDATE SET "
        " sample_interval_us=EXCLUDED.sample_interval_us, "
        " samples_per_trace=EXCLUDED.samples_per_trace, "
        " data_format_code=EXCLUDED.data_format_code;", SQL_NTS),
        stmt, SQL_HANDLE_STMT, "Prepare bin");

    odbc_check(SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &file_id, 0, NULL), stmt, SQL_HANDLE_STMT, "Bind p1");
    odbc_check(SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &sampleInterval, 0, NULL), stmt, SQL_HANDLE_STMT, "Bind p2");
    odbc_check(SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &numSamples, 0, NULL), stmt, SQL_HANDLE_STMT, "Bind p3");
    odbc_check(SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &formatCode, 0, NULL), stmt, SQL_HANDLE_STMT, "Bind p4");

    odbc_check(SQLExecute(stmt), stmt, SQL_HANDLE_STMT, "Exec bin");
    SQLFreeStmt(stmt, SQL_CLOSE);

    //trace headers (loop)
    const char* mt = std::getenv("MAX_TRACES");
    long long max_traces = (mt ? std::atoll(mt) : 2000); // 0 = all
    const size_t TRACE_HEADER_BYTES = 240;
    const size_t bytes_per_sample = bytes_per_sample_from_format(formatCode);

    SQLHSTMT tstmt = SQL_NULL_HSTMT;
    odbc_check(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &tstmt), tstmt, SQL_HANDLE_STMT, "Alloc STMT trace");
    odbc_check(SQLPrepare(tstmt, (SQLCHAR*)
      "INSERT INTO trace_headers (file_id, trace_seq, sample_interval_us, samples_in_trace, "
      "src_x, src_y, rcv_x, rcv_y, coord_scalar, units_code) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT (file_id, trace_seq) DO NOTHING;", SQL_NTS),
      tstmt, SQL_HANDLE_STMT, "Prepare trace");

    long long traceSeq=0; int tsi=0, tin=0, srcx=0, srcy=0, rcvx=0, rcvy=0, scal=0, units=0;

    odbc_check(SQLBindParameter(tstmt, 1, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &file_id, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp1");
    odbc_check(SQLBindParameter(tstmt, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &traceSeq, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp2");
    odbc_check(SQLBindParameter(tstmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &tsi, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp3");
    odbc_check(SQLBindParameter(tstmt, 4, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &tin, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp4");
    odbc_check(SQLBindParameter(tstmt, 5, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &srcx, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp5");
    odbc_check(SQLBindParameter(tstmt, 6, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &srcy, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp6");
    odbc_check(SQLBindParameter(tstmt, 7, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &rcvx, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp7");
    odbc_check(SQLBindParameter(tstmt, 8, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &rcvy, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp8");
    odbc_check(SQLBindParameter(tstmt, 9, SQL_PARAM_INPUT, SQL_C_SLONG,  SQL_INTEGER, 0, 0, &scal, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp9");
    odbc_check(SQLBindParameter(tstmt, 10, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &units, 0, NULL), tstmt, SQL_HANDLE_STMT, "tp10");

    std::vector<unsigned char> th(TRACE_HEADER_BYTES);

    while (true) {
        file.read(reinterpret_cast<char*>(th.data()), TRACE_HEADER_BYTES);
        if (!file) break; // EOF

        ++traceSeq;

        // Parse common fields (SEG-Y rev1 standard offsets)
        tsi   = be16(th.data(), 16);          // bytes 17-18  : sample interval (μs)
        tin   = be16(th.data(), 20);          // bytes 21-22  : samples in trace
        scal  = (int16_t)be16(th.data(), 68); // bytes 69-70  : scalar for coords (signed)
        srcx  = be32(th.data(), 72);          // bytes 73-76  : source X
        srcy  = be32(th.data(), 76);          // bytes 77-80  : source Y
        rcvx  = be32(th.data(), 80);          // bytes 81-84  : receiver group X
        rcvy  = be32(th.data(), 84);          // bytes 85-88  : receiver group Y
        units = be16(th.data(), 88);          // bytes 89-90  : coordinate units code

        odbc_check(SQLExecute(tstmt), tstmt, SQL_HANDLE_STMT, "Exec trace");

        if (max_traces > 0 && traceSeq >= max_traces) break;

        // Skip the trace samples to the next header
        const size_t samples = (tin > 0 ? (size_t)tin : (size_t)numSamples);
        const size_t bytes_to_skip = samples * bytes_per_sample;
        file.seekg((std::streamoff)bytes_to_skip, std::ios::cur);
        if (!file) break;
    }

    SQLFreeStmt(tstmt, SQL_CLOSE);

    // commit
    odbc_check(SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT), dbc, SQL_HANDLE_DBC, "COMMIT");

    // ---- cleanup ----
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);

    file.close();
    std::cout << "DB insert done. file_id=" << file_id << "\n";
    return 0;
}
