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
            --bg-dark: #0f0f1a;
            --bg-card: rgba(30, 30, 50, 0.8);
            --bg-card-hover: rgba(40, 40, 65, 0.9);
            --accent: #c9a227;
            --accent-light: #e8c547;
            --accent-dim: rgba(201, 162, 39, 0.3);
            --text-primary: #f5f5f5;
            --text-secondary: #a0a0b0;
            --text-muted: #606070;
            --border: rgba(255, 255, 255, 0.1);
            --success: #4ade80;
            --danger: #f87171;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'SF Pro Display', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg-dark);
            background-image:
                radial-gradient(ellipse at top, rgba(201, 162, 39, 0.15) 0%, transparent 50%),
                radial-gradient(ellipse at bottom, rgba(30, 30, 50, 0.5) 0%, transparent 50%);
            min-height: 100vh;
            padding: 20px;
            color: var(--text-primary);
        }

        .container { max-width: 800px; margin: 0 auto; }

        .header {
            text-align: center;
            margin-bottom: 32px;
        }

        .logo {
            font-size: 2.5em;
            font-weight: 300;
            letter-spacing: 8px;
            color: var(--text-primary);
            text-transform: uppercase;
            margin-bottom: 8px;
        }

        .logo span { color: var(--accent); }

        .tagline {
            font-size: 0.9em;
            color: var(--text-muted);
            letter-spacing: 2px;
        }

        .navbar {
            display: flex;
            justify-content: center;
            gap: 8px;
            margin-bottom: 32px;
            padding: 6px;
            background: var(--bg-card);
            border-radius: 50px;
            backdrop-filter: blur(10px);
            border: 1px solid var(--border);
            width: fit-content;
            margin-left: auto;
            margin-right: auto;
        }

        .nav-link {
            color: var(--text-secondary);
            text-decoration: none;
            padding: 10px 24px;
            border-radius: 50px;
            font-weight: 500;
            font-size: 0.9em;
            transition: all 0.3s ease;
        }

        .nav-link:hover {
            color: var(--text-primary);
            background: rgba(255, 255, 255, 0.05);
        }

        .nav-link.active {
            background: var(--accent);
            color: var(--bg-dark);
            font-weight: 600;
        }

        .card {
            background: var(--bg-card);
            border-radius: 20px;
            padding: 24px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid var(--border);
        }

        .card-title {
            font-size: 0.8em;
            font-weight: 600;
            letter-spacing: 2px;
            text-transform: uppercase;
            color: var(--accent);
            margin-bottom: 20px;
        }

        .upload-area {
            border: 2px dashed var(--border);
            border-radius: 16px;
            padding: 48px 24px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s ease;
            background: rgba(0, 0, 0, 0.2);
            margin-bottom: 24px;
        }

        .upload-area:hover {
            border-color: var(--accent);
            background: var(--accent-dim);
        }

        .upload-icon {
            font-size: 48px;
            margin-bottom: 16px;
            opacity: 0.5;
        }

        .upload-text {
            color: var(--text-secondary);
            font-size: 1em;
        }

        .upload-text strong {
            color: var(--accent);
        }

        .file-list {
            max-height: 500px;
            overflow-y: auto;
        }

        .file-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 16px;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 12px;
            margin-bottom: 8px;
            transition: all 0.2s ease;
        }

        .file-item:hover {
            background: rgba(0, 0, 0, 0.3);
        }

        .file-thumb {
            width: 60px;
            height: 60px;
            border-radius: 8px;
            background: rgba(0,0,0,0.3);
            margin-right: 16px;
            object-fit: cover;
            flex-shrink: 0;
        }

        .file-info { flex: 1; }

        .file-name {
            font-weight: 500;
            color: var(--text-primary);
            margin-bottom: 4px;
        }

        .file-size {
            font-size: 0.8em;
            color: var(--text-muted);
        }

        .btn-delete {
            padding: 8px 16px;
            font-size: 0.85em;
            background: rgba(248, 113, 113, 0.2);
            color: var(--danger);
            border: 1px solid rgba(248, 113, 113, 0.3);
            border-radius: 8px;
            cursor: pointer;
            transition: all 0.2s ease;
            font-weight: 500;
        }

        .btn-delete:hover {
            background: var(--danger);
            color: #000;
        }

        .empty-state {
            text-align: center;
            color: var(--text-muted);
            padding: 48px 20px;
        }

        .loading {
            opacity: 0.6;
            pointer-events: none;
        }

        ::-webkit-scrollbar { width: 6px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: var(--text-muted); border-radius: 3px; }

        @media (max-width: 768px) {
            body { padding: 12px; }
            .logo { font-size: 1.8em; letter-spacing: 4px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">Sisy<span>phus</span></div>
            <div class="tagline">File Management</div>
        </div>

        <nav class="navbar">
            <a href="/" class="nav-link">Patterns</a>
            <a href="/manual" class="nav-link">Manual</a>
            <a href="/files" class="nav-link active">Files</a>
            <a href="/tuning" class="nav-link">Tuning</a>
        </nav>

        <div class="card">
            <div class="card-title">Upload Pattern</div>
            <div class="upload-area" id="upload-area">
                <div class="upload-icon">+</div>
                <p class="upload-text">Click or drag <strong>.thr</strong> file to upload</p>
                <input type="file" id="file-input" accept=".thr" style="display:none">
                <input type="file" id="image-input" accept="image/png,image/jpeg" style="display:none">
            </div>

            <div class="card-title">Pattern Files</div>
            <div class="file-list" id="file-list">
                <div class="empty-state">Loading files...</div>
            </div>
        </div>
    </div>

    <script>
        const apiBase = '/api';
        let storageAvailable = true;

        async function loadFileList() {
            const response = await fetch(apiBase + '/files');
            const data = await response.json();
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
                imageInput.onchange = async () => {
                    const imageFile = imageInput.files[0];
                    await performUpload(file, imageFile);
                };
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
            uploadArea.style.borderColor = 'var(--accent)';
            uploadArea.style.background = 'var(--accent-dim)';
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
