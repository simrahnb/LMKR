import { useState } from "react";

export default function UploadForm() {
  const [status, setStatus] = useState("");

  async function handleSubmit(e) {
    e.preventDefault();
    const file = e.target.file.files[0];
    if (!file) return alert("Select a SEG-Y file first!");

    const form = new FormData();
    form.append("file", file);

    try {
      const res = await fetch("http://localhost:3004/upload", {
        method: "POST",
        body: form,
      });
      const json = await res.json();
      setStatus(JSON.stringify(json, null, 2));
    } catch (err) {
      setStatus("Upload failed: " + err.message);
    }
  }

  return (
    <div style={{ padding: "2rem" }}>
      <h2>Upload SEG-Y File</h2>
      <form onSubmit={handleSubmit}>
        <input type="file" name="file" accept=".sgy,.segy" />
        <button type="submit">Upload</button>
      </form>
      <pre>{status}</pre>
    </div>
  );
}
