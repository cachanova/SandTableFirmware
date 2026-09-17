#pragma once

const char FILE_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sisyphus Files</title>
    <style>
        :root {
            --paper: #fafaf7;
            --sand: #eeeade;
            --ink: #1a1917;
            --ink-soft: #57544e;
            --ink-faint: #97938a;
            --hair: #dbd8cf;
            --wash: #f2f1ea;
            --ok: #3d6b3d;
            --danger: #9e3a2e;
            --mono: "SF Mono", ui-monospace, Menlo, Consolas, monospace;
            --serif: Georgia, "Times New Roman", serif;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: -apple-system, "Segoe UI", "Helvetica Neue", Arial, sans-serif;
            background: var(--paper);
            color: var(--ink);
            min-height: 100vh;
            font-size: 14px;
        }

        .topbar {
            display: flex; align-items: baseline; justify-content: space-between;
            gap: 18px; flex-wrap: wrap;
            padding: 22px 40px; border-bottom: 1px solid var(--ink);
        }
        .brand { font-family: var(--serif); font-size: 20px; letter-spacing: .5px; white-space: nowrap; }
        .brand i { font-style: normal; border-bottom: 3px double var(--ink); padding-bottom: 2px; }
        .brand span { color: var(--ink-faint); font-size: 14px; margin-left: 6px; }
        .topnav { display: flex; gap: 30px; }
        .topnav a { color: var(--ink-faint); text-decoration: none; font-size: 12px; letter-spacing: 2.5px; text-transform: uppercase; padding-bottom: 3px; }
        .topnav a:hover { color: var(--ink-soft); }
        .topnav a.active { color: var(--ink); border-bottom: 1px solid var(--ink); }

        .container { max-width: 680px; margin: 0 auto; padding: 38px 24px 80px; }

        .rule-head {
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 3px;
            text-transform: uppercase;
            padding-bottom: 10px;
            border-bottom: 1px solid var(--ink);
            margin-bottom: 14px;
        }
        section { margin-bottom: 44px; }

        .upload-area {
            border: 1px dashed var(--ink-faint);
            padding: 42px 24px;
            text-align: center;
            cursor: pointer;
            transition: all .15s;
            background: none;
        }
        .upload-area:hover { border-color: var(--ink); background: var(--wash); }
        .upload-icon { font-family: var(--serif); font-size: 34px; color: var(--ink-faint); margin-bottom: 10px; line-height: 1; }
        .upload-text { color: var(--ink-soft); font-size: 13.5px; }
        .upload-text strong { color: var(--ink); }

        .file-list { max-height: 520px; overflow-y: auto; }
        .file-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 14px;
            padding: 11px 2px;
            border-bottom: 1px solid var(--hair);
        }
        .file-item:hover { background: var(--wash); }
        .file-info { flex: 1; display: flex; align-items: baseline; justify-content: space-between; gap: 12px; min-width: 0; }
        .file-name {
            font-family: var(--serif);
            font-size: 16px;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .file-size { font-family: var(--mono); font-size: 11px; color: var(--ink-faint); flex: none; }
        .btn-delete {
            padding: 6px 14px;
            font-size: 10px;
            font-weight: 600;
            letter-spacing: 1.5px;
            text-transform: uppercase;
            background: none;
            color: var(--danger);
            border: 1px solid var(--danger);
            cursor: pointer;
            transition: all .12s;
            font-family: inherit;
            flex: none;
        }
        .btn-delete:hover { background: var(--danger); color: var(--paper); }

        .empty-state {
            color: var(--ink-faint);
            padding: 32px 2px;
            font-style: italic;
            font-family: var(--serif);
            font-size: 14px;
        }

        .loading { opacity: .5; pointer-events: none; }

        ::-webkit-scrollbar { width: 6px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: var(--hair); }

        @media (max-width: 560px) {
            .topbar { padding: 16px 20px; }
            .topnav { gap: 16px; }
            .container { padding: 24px 16px 60px; }
        }
    </style>
</head>
<body>
    <header class="topbar">
        <div class="brand"><i>Sisyphus</i><span>Files</span></div>
        <nav class="topnav">
            <a href="/">Patterns</a>
            <a href="/manual">Manual</a>
            <a href="/files" class="active">Files</a>
            <a href="/tuning">Tuning</a>
        </nav>
    </header>

    <div class="container">
        <section>
            <h2 class="rule-head">Upload Pattern</h2>
            <div class="upload-area" id="upload-area">
                <div class="upload-icon">+</div>
                <p class="upload-text">Click or drag <strong>.thr</strong> file to upload</p>
                <input type="file" id="file-input" accept=".thr" style="display:none">
                <input type="file" id="image-input" accept="image/png,image/jpeg" style="display:none">
            </div>
        </section>

        <section>
            <h2 class="rule-head">Pattern Files</h2>
            <div class="file-list" id="file-list">
                <div class="empty-state">Loading files...</div>
            </div>
        </section>
    </div>

    <script>
        async function uploadPatternThumbnail(apiBase, imageFile, imageName) {
            // Keep full-size uploads for the canvas; list previews need only 128px.
            let bitmap;
            try {
                bitmap = await createImageBitmap(imageFile);
                const canvas = document.createElement('canvas');
                canvas.width = canvas.height = 128;
                const scale = 128 / Math.max(bitmap.width, bitmap.height);
                const width = bitmap.width * scale, height = bitmap.height * scale;
                canvas.getContext('2d').drawImage(bitmap, (128 - width) / 2, (128 - height) / 2, width, height);
                const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png'));
                if (!blob) return;
                const formData = new FormData();
                formData.append('file', blob, imageName);
                const abort = new AbortController();
                const timeout = setTimeout(() => abort.abort(), 8000);
                try {
                    const response = await fetch(apiBase + '/files/upload?thumbnail=1',
                        { method: 'POST', body: formData, signal: abort.signal });
                    if (!response.ok) throw new Error('Thumbnail upload failed');
                } finally {
                    clearTimeout(timeout);
                }
            } catch (error) {
                // The original image remains usable when thumbnail generation/upload fails.
                console.warn('Small preview unavailable:', error);
            } finally {
                if (bitmap) bitmap.close();
            }
        }

        const apiBase = '/api';
        let storageAvailable = true;

        let fileListInFlight = false;
        let fileListRetry;
        async function loadFileList() {
            if (fileListInFlight) return;
            fileListInFlight = true;
            clearTimeout(fileListRetry);
            const abort = new AbortController();
            const timeout = setTimeout(() => abort.abort(), 4000);
            try {
                const response = await fetch(apiBase + '/files', {signal: abort.signal, cache: 'no-store'});
                if (!response.ok) throw new Error(response.status === 503 ? 'Loading pattern library…' : 'Pattern library unavailable. Retrying…');
                const data = await response.json();
                if (data.loading) fileListRetry = setTimeout(loadFileList, 750);
                const fileList = document.getElementById('file-list');
                fileList.innerHTML = '';
                storageAvailable = data.storageAvailable !== false;
                const uploadArea = document.getElementById('upload-area');
                uploadArea.style.pointerEvents = storageAvailable ? '' : 'none';
                uploadArea.style.opacity = storageAvailable ? '' : '0.5';

                if (!storageAvailable) {
                    uploadArea.querySelector('.upload-text').textContent = 'SD card not detected — uploads unavailable';
                    fileList.innerHTML = '<div class="empty-state">SD card not detected — patterns unavailable</div>';
                    return;
                }

                if (data.files && data.files.length > 0) {
                    data.files.forEach(file => {
                        const displayName = file.name.replace('.thr', '');
                        const thumbUrl = apiBase + '/pattern/image?file=' + encodeURIComponent(displayName);
                        const fileItem = document.createElement('div');
                        fileItem.className = 'file-item';
                        const info = document.createElement('div');
                        info.className = 'file-info';
                        const name = document.createElement('div');
                        name.className = 'file-name';
                        name.textContent = displayName;
                        const size = document.createElement('div');
                        size.className = 'file-size';
                        size.textContent = file.size > 0 ? (file.size / 1024).toFixed(1) + ' KB' : '';
                        const remove = document.createElement('button');
                        remove.className = 'btn-delete';
                        remove.textContent = 'Delete';
                        remove.addEventListener('click', () => deleteFile(file.name));
                        info.appendChild(name);
                        info.appendChild(size);
                        fileItem.appendChild(info);
                        fileItem.appendChild(remove);
                        fileList.appendChild(fileItem);
                    });
                } else {
                    fileList.innerHTML = '<div class="empty-state">No pattern files found</div>';
                }
            } catch (error) {
                const list = document.getElementById('file-list');
                if (!list.querySelector('.file-item')) list.textContent = error.message || 'Pattern library unavailable. Retrying…';
                fileListRetry = setTimeout(loadFileList, 1000);
            } finally {
                clearTimeout(timeout);
                fileListInFlight = false;
            }
        }

        async function deleteFile(filename) {
            if (!confirm('Delete ' + filename.replace('.thr', '') + '?')) return;
            const formData = new FormData();
            formData.append('file', filename);
            await fetch(apiBase + '/files/delete', { method: 'POST', body: formData });
            await loadFileList();
        }

        async function uploadFile(file) {
            if (!storageAvailable) {
                alert('Insert an SD card before uploading patterns.');
                return;
            }
            if (!file) return;
            if (!file.name.endsWith('.thr')) {
                alert('Only .thr files are allowed');
                return;
            }

            if (confirm('Do you want to add a preview image for this pattern?')) {
                const imageInput = document.getElementById('image-input');
                imageInput.value = '';
                // Runs on selection or on picker cancel (uploads without image)
                const handleImage = async () => {
                    imageInput.onchange = null;
                    imageInput.oncancel = null;
                    const imageFile = imageInput.files[0] || null;
                    await performUpload(file, imageFile);
                };
                imageInput.onchange = handleImage;
                imageInput.oncancel = handleImage;
                imageInput.click();
            } else {
                await performUpload(file, null);
            }
        }

        async function performUpload(patternFile, imageFile) {
            const uploadArea = document.getElementById('upload-area');
            uploadArea.classList.add('loading');
            uploadArea.querySelector('.upload-text').textContent = 'Uploading...';

            try {
                // Upload pattern
                const fdPattern = new FormData();
                fdPattern.append('file', patternFile);
                const patternResponse = await fetch(apiBase + '/files/upload', { method: 'POST', body: fdPattern });
                if (!patternResponse.ok) throw new Error((await patternResponse.json()).message || 'Pattern upload failed');

                // Upload image if present
                if (imageFile) {
                    const fdImage = new FormData();
                    let basename = patternFile.name;
                    if (basename.endsWith('.thr')) basename = basename.substring(0, basename.length - 4);
                    const imageName = basename + '.png';
                    fdImage.append('file', imageFile, imageName);
                    const imageResponse = await fetch(apiBase + '/files/upload', { method: 'POST', body: fdImage });
                    if (!imageResponse.ok) throw new Error((await imageResponse.json()).message || 'Image upload failed');
                    await uploadPatternThumbnail(apiBase, imageFile, imageName);
                }

                await loadFileList();
            } catch (err) {
                alert('Upload failed: ' + err.message);
            } finally {
                uploadArea.classList.remove('loading');
                uploadArea.querySelector('.upload-text').innerHTML = 'Click or drag <strong>.thr</strong> file to upload';
            }
        }

        const uploadArea = document.getElementById('upload-area');
        const fileInput = document.getElementById('file-input');

        uploadArea.addEventListener('click', () => fileInput.click());
        fileInput.addEventListener('change', (e) => uploadFile(e.target.files[0]));

        uploadArea.addEventListener('dragover', (e) => {
            e.preventDefault();
            uploadArea.style.borderColor = 'var(--ink)';
            uploadArea.style.background = 'var(--wash)';
        });

        uploadArea.addEventListener('dragleave', () => {
            uploadArea.style.borderColor = '';
            uploadArea.style.background = '';
        });

        uploadArea.addEventListener('drop', (e) => {
            e.preventDefault();
            uploadArea.style.borderColor = '';
            uploadArea.style.background = '';
            if (e.dataTransfer.files.length > 0) {
                uploadFile(e.dataTransfer.files[0]);
            }
        });

        window.onload = loadFileList;
    </script>
</body>
</html>
)rawliteral";
