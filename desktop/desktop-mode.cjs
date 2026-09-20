function desktopWorkArea(screen) {
  return screen.getPrimaryDisplay().workArea;
}

const DESKTOP_WINDOW_SIZE = 720;

function desktopWindowBounds(screen) {
  const area = desktopWorkArea(screen);
  return {
    x: area.x + Math.max(0, area.width - DESKTOP_WINDOW_SIZE),
    y: area.y + Math.max(0, area.height - DESKTOP_WINDOW_SIZE),
    width: DESKTOP_WINDOW_SIZE,
    height: DESKTOP_WINDOW_SIZE,
  };
}

function shouldShowDesktopWidget({ rendererReady, host, manuallyHidden }) {
  return Boolean(rendererReady && host?.hostAlive && !manuallyHidden);
}

module.exports = { desktopWorkArea, desktopWindowBounds, shouldShowDesktopWidget };
