import { app, BrowserWindow, shell, utilityProcess } from 'electron';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

let serverProcess = null;
let mainWindow = null;
const PORT = 3491;

function getUnpackedDir() {
  return __dirname.includes('app.asar')
    ? __dirname.replace('app.asar', 'app.asar.unpacked')
    : __dirname;
}

async function startServer() {
  return new Promise((resolve) => {
    const unpackedDir = getUnpackedDir();
    const serverPath = join(unpackedDir, 'server.js');

    serverProcess = utilityProcess.fork(serverPath, [], {
      env: {
        ...process.env,
        PORT: String(PORT),
        ELECTRON_APP: '1',
        ELECTRON_RESOURCE_DIR: unpackedDir,
      },
      cwd: unpackedDir,
      stdio: 'pipe',
    });

    serverProcess.stdout?.on('data', (d) => {
      const msg = d.toString();
      if (msg.includes('listen') || msg.includes(String(PORT))) resolve();
    });

    serverProcess.on('exit', (code) => {
      console.error('Server exited with code:', code);
    });

    // Give it 8s max
    setTimeout(resolve, 8000);
  });
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 1024,
    minHeight: 680,
    titleBarStyle: 'hiddenInset',
    trafficLightPosition: { x: 12, y: 10 },
    backgroundColor: '#000000',
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
    },
  });

  mainWindow.loadURL(`http://localhost:${PORT}/`);

  // Inject a draggable titlebar strip so traffic lights don't overlap nav content
  mainWindow.webContents.on('did-finish-load', () => {
    mainWindow?.webContents.insertCSS(`
      body::before {
        content: '';
        display: block;
        height: 28px;
        width: 100%;
        position: fixed;
        top: 0;
        left: 0;
        z-index: 9999;
        -webkit-app-region: drag;
        background: transparent;
      }
      .dashboard-nav, .can-nav, .auth-page, .dash-nav {
        padding-top: 28px !important;
      }
    `);
  });

  mainWindow.webContents.on('did-fail-load', (_e, code, desc) => {
    console.log('Load failed:', code, desc, '— retrying in 2s');
    setTimeout(() => mainWindow?.loadURL(`http://localhost:${PORT}/`), 2000);
  });

  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });

  mainWindow.on('closed', () => { mainWindow = null; });
}

app.whenReady().then(async () => {
  await startServer();
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (serverProcess) serverProcess.kill();
  if (process.platform !== 'darwin') app.quit();
});

app.on('before-quit', () => {
  if (serverProcess) serverProcess.kill();
});
