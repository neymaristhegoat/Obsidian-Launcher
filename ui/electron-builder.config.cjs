module.exports = {
  appId: 'dev.obsidian.launcher',
  productName: 'Obsidian Launcher',
  artifactName: '${productName}-${version}-${os}-${arch}.${ext}',
  directories: {
    output: process.env.OBSIDIAN_ARTIFACT_DIR || '../artifacts',
    buildResources: '../resources',
  },
  files: [
    'dist/**/*',
    'dist-electron/**/*',
    'package.json',
    '!accounts.json',
    '!.env*',
  ],
  extraResources: [
    {
      from: '../build/core',
      to: 'core',
      filter: ['launcher_core_cli*'],
    },
    {
      from: '../resources/icon.png',
      to: 'icon.png',
    },
  ],
  asar: true,
  publish: null,
  win: {
    icon: 'icon.ico',
    target: ['nsis', 'portable'],
  },
  nsis: {
    oneClick: false,
    allowToChangeInstallationDirectory: true,
    createDesktopShortcut: true,
    createStartMenuShortcut: true,
  },
  mac: {
    icon: 'icon.icns',
    target: ['dmg', 'zip'],
    category: 'public.app-category.games',
  },
  linux: {
    icon: 'icon.png',
    target: ['tar.gz'],
    category: 'Game',
    syncDesktopName: true,
  },
}
