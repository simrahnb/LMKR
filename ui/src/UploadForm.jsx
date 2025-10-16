import { useState } from "react";

export default function UploadForm() {
  const [status, setStatus] = useState("");
  const [busy, setBusy] = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    const file = e.target.file?.files?.[0];
    if (!file) { alert("Select a SEG-Y file first!"); return; }

    const form = new FormData();
    form.append("file", file);

    setBusy(true);
    setStatus("Uploading… this may take a minute for large files.");

    try {
      const res = await fetch("/upload", {
        method: "POST",
        body: form,
      });

      const text = await res.text();
      if (!res.ok) throw new Error(`${res.status} ${text || res.statusText}`);

      let json;
      try { json = JSON.parse(text); }
      catch { json = { ok: true, message: "Uploaded", raw: text }; }

    
      const lines = (json.output || "").split("\n");
      const sampleInterval = lines.find(l => l.includes("Sample Interval")) || "";
      const samplesPerTrace = lines.find(l => l.includes("Samples per Trace")) || "";
      const dataFormatCode  = lines.find(l => l.includes("Data Format Code")) || "";

      const summary = [
        `✅ ${json.message || "OK"}`,
        json.saved_as ? `Saved as: ${json.saved_as}` : null,
        sampleInterval,
        samplesPerTrace,
        dataFormatCode,
      ].filter(Boolean).join("\n");

      setStatus(summary || JSON.stringify(json, null, 2));
      // 🔼🔼🔼 END SUMMARY BLOCK 🔼🔼🔼

    } catch (err) {
      setStatus("❌ Upload failed: " + (err?.message || String(err)));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div style={{ padding: "2rem" }}>
      <h2>Upload SEG-Y File</h2>
      <form onSubmit={handleSubmit}>
        <input type="file" name="file" accept=".sgy,.segy,.tra" disabled={busy} />
        <button type="submit" disabled={busy}>{busy ? "Uploading…" : "Upload"}</button>
      </form>
      <pre style={{ marginTop: 12, background: "#f6f8fa", padding: 12, borderRadius: 6 }}>
        {status}
      </pre>
    </div>
  );
}
