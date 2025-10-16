/* eslint-disable no-console */
const express = require('express');
const cors = require('cors');
const multer = require('multer');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const { execFile } = require('child_process');
require('dotenv').config();
const { Pool } = require('pg');

const app = express();

/**  Dev logger: see every request in the terminal  */
app.use((req, _res, next) => {
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.url}`);
  next();
});

/**  CORS: permissive for local dev (works with localhost or 127.0.0.1)  */
app.use(cors({ origin: true, credentials: true }));
app.options('/upload', cors({ origin: true, credentials: true })); // preflight

/**  Keep the connection open for longer ingests  */
app.use((req, res, next) => {
  res.setTimeout(10 * 60 * 1000); // 10 minutes
  next();
});

/**  Paths  */
const cppDir  = path.join(__dirname, '..', 'cpp');
const dataDir = path.join(cppDir, 'data');
fs.mkdirSync(dataDir, { recursive: true });

/**  Safer filename helper  */
function safeName(original) {
  const ts = Date.now();
  const base = path.basename(original).replace(/\s+/g, '_').replace(/[^A-Za-z0-9._-]/g, '');
  return `${ts}__${base}`;
}

/**  Multer temp dir  */
const tempDir = path.join(__dirname, 'uploads');
fs.mkdirSync(tempDir, { recursive: true });

const upload = multer({
  dest: tempDir,
  limits: { fileSize: 1024 * 1024 * 1024 }, // 1 GB
  fileFilter: (_req, file, cb) => {
    if (!/\.(sgy|segy|tra)$/i.test(file.originalname)) {
      return cb(new Error('Only .sgy, .segy, .tra are allowed'));
    }
    cb(null, true);
  },
});

/**  PG pool (env first, fallback to your local PG on 5433)  */
const pg = new Pool({
  host: process.env.PGHOST || 'localhost',
  port: +(process.env.PGPORT || 5433),
  user: process.env.PGUSER || 'simrah',
  password: process.env.PGPASSWORD || 'secret',
  database: process.env.PGDATABASE || 'seismics',
});

/**  SHA256 stream helper (no big RAM spike)  */
function sha256FileStream(absPath) {
  return new Promise((resolve, reject) => {
    const h = crypto.createHash('sha256');
    const s = fs.createReadStream(absPath);
    s.on('error', reject);
    h.on('error', reject);
    s.on('data', (chunk) => h.update(chunk));
    s.on('end', () => resolve(h.digest('hex')));
  });
}

/**  GET /files: recent ingests for UI table  */
app.get('/files', async (_req, res) => {
  try {
    const r = await pg.query(`
      SELECT f.id, f.file_path, f.sha256, f.filesize, f.created_at,
             bh.sample_interval_us, bh.samples_per_trace, bh.data_format_code
      FROM files f
      LEFT JOIN binary_headers bh ON bh.file_id = f.id
      ORDER BY f.id DESC
      LIMIT 20
    `);
    res.json({ ok: true, rows: r.rows });
  } catch (e) {
    console.error('GET /files error:', e);
    res.status(500).json({ ok: false, error: e.message });
  }
});

/**  POST /upload: receive file → de-dupe → move → run ingester  */
app.post('/upload', upload.single('file'), async (req, res) => {
  if (!req.file) return res.status(400).json({ ok: false, error: 'No file uploaded' });

  const finalName = safeName(req.file.originalname);
  const destAbs   = path.join(dataDir, finalName); // absolute path to cpp/data/<file>

  try {
    // Compute SHA256 + filesize on the temp file before moving
    const tempAbs = req.file.path;
    const size = fs.statSync(tempAbs).size;
    const hash = await sha256FileStream(tempAbs);
    console.log(` SHA256=${hash} size=${size} bytes`);

    // DB de-dupe by content
    const existing = await pg.query('SELECT id, file_path FROM files WHERE sha256=$1', [hash]);
    if (existing.rows.length) {
      // remove temp file since we won’t need it
      try { fs.unlinkSync(tempAbs); } catch {}
      const row = existing.rows[0];
      console.log('⚠️ Duplicate by sha256. Skipping ingest. file_id=%s path=%s', row.id, row.file_path);
      return res.json({
        ok: true,
        message: 'Duplicate file skipped (same content).',
        duplicate_of: { id: row.id, file_path: row.file_path },
      });
    }

    // Move file from temp to cpp/data (unique name prevents file_path conflict)
    await fs.promises.rename(tempAbs, destAbs);
    console.log('📁 Stored at:', destAbs);

    // Run C++ ingester (must already be built as cpp/ingest_sgy and executable)
    const ingester = path.join(cppDir, 'ingest_sgy');

    // Child env so ODBC can find your driver & DSN
    const childEnv = {
      ...process.env,
      ODBCSYSINI: "/etc",
      ODBCINI: process.env.ODBCINI || (process.env.HOME + "/.odbc.ini"), // your DSN file
      PATH: `/opt/homebrew/bin:/usr/local/bin:${process.env.PATH || ''}`,
      DYLD_LIBRARY_PATH: "/opt/homebrew/lib:/opt/homebrew/opt/libpq/lib:/opt/homebrew/opt/unixodbc/lib",
    };

    console.log(' Running ingester:', ingester, destAbs);

    // Use ABSOLUTE path; C++ reads argv[1] directly and inserts/updates DB rows
    execFile(ingester, [destAbs], { env: childEnv }, async (error, stdout, stderr) => {
      console.log('📤 Ingest finished');
      if (error) {
        console.error('Ingest error:', error);
        if (stderr) console.error('STDERR:', stderr);
        return res.status(500).json({
          ok: false,
          error: 'Ingest failed',
          detail: (stderr && stderr.trim()) || error.message,
          saved_as: destAbs,
        });
      }
      console.log('Ingest OK:\n' + stdout);

      // After the C++ tool inserted/updated the files row (by file_path), attach sha256 + filesize
      try {
        await pg.query(
          'UPDATE files SET sha256=$1, filesize=$2 WHERE file_path=$3',
          [hash, size, destAbs]
        );
      } catch (e) {
        console.error('Post-ingest UPDATE files failed:', e);
        // Don’t fail the whole request; return success but include a warning
        return res.json({
          ok: true,
          message: 'Ingested successfully (but failed to save sha256/filesize)',
          saved_as: destAbs,
          output: stdout,
          warnings: (stderr && stderr.trim() ? stderr + '\n' : '') + (e.message || 'update failed'),
        });
      }

      return res.json({
        ok: true,
        message: 'Ingested successfully',
        saved_as: destAbs,
        output: stdout,
        warnings: stderr && stderr.trim() ? stderr : null,
      });
    });
  } catch (err) {
    console.error('Upload pipeline error:', err);
    // clean up temp file on error if it still exists
    try { if (req.file?.path && fs.existsSync(req.file.path)) fs.unlinkSync(req.file.path); } catch {}
    return res.status(500).json({ ok: false, error: err.message || String(err) });
  }
});

/**  Health check  */
app.get('/', (_req, res) => res.json({ ok: true, port: 3001 }));

/**  Start server  */
const server = app.listen(3001, '0.0.0.0', () =>
  console.log('✅ API running on http://127.0.0.1:3001')
);
server.on('error', (e) => console.error('server error:', e));
server.on('close', () => console.log('server closed'));
