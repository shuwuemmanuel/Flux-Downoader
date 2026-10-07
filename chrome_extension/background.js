// Flux Downloader - background service worker
// Intercepts browser downloads (and manually chosen links) and forwards
// them to the local Flux Downloader app instead of letting Chrome handle them.

const DEFAULT_PORT = 38019;

async function getPort() {
  const { fluxPort } = await chrome.storage.local.get("fluxPort");
  return fluxPort || DEFAULT_PORT;
}

async function isInterceptEnabled() {
  const { interceptEnabled } = await chrome.storage.local.get("interceptEnabled");
  return interceptEnabled !== false; // default ON
}

async function sendToFlux(url, filename, referrer, kind) {
  const port = await getPort();
  try {
    const resp = await fetch(`http://127.0.0.1:${port}/add`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ url, filename: filename || "", referrer: referrer || "", kind: kind || "generic" }),
    });
    return resp.ok;
  } catch (e) {
    return false;
  }
}

// Extension size threshold: only intercept files above this size (bytes) to
// avoid grabbing tiny inline assets accidentally triggered as "downloads".
const MIN_INTERCEPT_BYTES = 0;

chrome.downloads.onCreated.addListener(async (downloadItem) => {
  const enabled = await isInterceptEnabled();
  if (!enabled) return;

  // Avoid intercepting our own extension's data URLs, etc.
  if (!downloadItem.url || downloadItem.url.startsWith("blob:") || downloadItem.url.startsWith("data:")) {
    return;
  }

  const sent = await sendToFlux(downloadItem.url, downloadItem.filename, downloadItem.referrer, "generic");
  if (sent) {
    try {
      await chrome.downloads.cancel(downloadItem.id);
      await chrome.downloads.erase({ id: downloadItem.id });
      chrome.notifications.create({
        type: "basic",
        iconUrl: "icons/icon48.png",
        title: "Flux Downloader",
        message: "Download sent to Flux Downloader.",
      });
    } catch (e) {
      // ignore - download may have already completed before we could cancel
    }
  }
});

// Right-click context menu on links / videos
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "flux-download-link",
    title: "Download with Flux Downloader",
    contexts: ["link"],
  });
  chrome.contextMenus.create({
    id: "flux-download-media",
    title: "Download media with Flux Downloader",
    contexts: ["video", "audio", "image"],
  });
});

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  const url = info.linkUrl || info.srcUrl;
  if (!url) return;
  const kind = (tab && tab.url && (tab.url.includes("youtube.com") || tab.url.includes("youtu.be"))) ? "youtube_video" : "generic";
  await sendToFlux(url, "", tab ? tab.url : "", kind);
  chrome.notifications.create({
    type: "basic",
    iconUrl: "icons/icon48.png",
    title: "Flux Downloader",
    message: "Link sent to Flux Downloader.",
  });
});

// Catch magnet: links before Chrome tries to hand them off to an external
// torrent client / shows the "open in app?" prompt, and send straight to
// Flux Downloader's built-in torrent engine instead.
chrome.webNavigation.onBeforeNavigate.addListener(async (details) => {
  if (details.frameId !== 0) return;
  if (!details.url || !details.url.startsWith("magnet:")) return;
  const enabled = await isInterceptEnabled();
  if (!enabled) return;
  const sent = await sendToFlux(details.url, "", "", "magnet");
  if (sent) {
    chrome.notifications.create({
      type: "basic",
      iconUrl: "icons/icon48.png",
      title: "Flux Downloader",
      message: "Magnet link sent to Flux Downloader.",
    });
  }
});

// Messages from content_script.js / popup.js
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message.type === "FLUX_SEND_URL") {
    sendToFlux(message.url, message.filename, message.referrer, message.kind).then((ok) => {
      sendResponse({ ok });
    });
    return true; // keep channel open for async sendResponse
  }
  if (message.type === "FLUX_PING") {
    getPort().then(async (port) => {
      try {
        const resp = await fetch(`http://127.0.0.1:${port}/ping`);
        sendResponse({ connected: resp.ok });
      } catch (e) {
        sendResponse({ connected: false });
      }
    });
    return true;
  }
});
