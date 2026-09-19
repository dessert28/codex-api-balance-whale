function desktopWorkArea(screen) {
  return screen.getPrimaryDisplay().workArea;
}

function shouldShowDesktopWidget({ rendererReady, host, manuallyHidden }) {
  return Boolean(rendererReady && host?.hostAlive && !manuallyHidden);
}

module.exports = { desktopWorkArea, shouldShowDesktopWidget };
