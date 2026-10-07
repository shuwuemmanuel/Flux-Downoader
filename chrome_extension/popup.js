const dot = document.getElementById("dot");
const statusText = document.getElementById("statusText");
const interceptToggle = document.getElementById("interceptToggle");
const iconsToggle = document.getElementById("iconsToggle");
const urlInput = document.getElementById("urlInput");
const sendBtn = document.getElementById("sendBtn");

function refreshStatus() {
  chrome.runtime.sendMessage({ type: "FLUX_PING" }, (resp) => {
    if (resp && resp.connected) {
      dot.className = "dot on";
      statusText.textContent = "Connected to Flux Downloader";
    } else {
      dot.className = "dot off";
      statusText.textContent = "Flux Downloader not detected";
    }
  });
}

// Load intercept toggle state
chrome.storage.local.get("interceptEnabled", ({ interceptEnabled }) => {
  interceptToggle.checked = interceptEnabled !== false;
});

interceptToggle.addEventListener("change", () => {
  chrome.storage.local.set({ interceptEnabled: interceptToggle.checked });
});

// Load download icons toggle state
chrome.storage.local.get("downloadIconsEnabled", ({ downloadIconsEnabled }) => {
  iconsToggle.checked = downloadIconsEnabled !== false; // default true
});

iconsToggle.addEventListener("change", () => {
  const enabled = iconsToggle.checked;
  chrome.storage.local.set({ downloadIconsEnabled: enabled });
  
  // Notify all tabs to update
  chrome.tabs.query({}, (tabs) => {
    tabs.forEach(tab => {
      chrome.tabs.sendMessage(tab.id, {
        type: "TOGGLE_DOWNLOAD_ICONS",
        enabled: enabled
      }).catch(() => {}); // Ignore errors for tabs without content script
    });
  });
});

sendBtn.addEventListener("click", () => {
  const url = urlInput.value.trim();
  if (!url) return;
  chrome.runtime.sendMessage({ type: "FLUX_SEND_URL", url }, (resp) => {
    statusText.textContent = resp && resp.ok ? "Sent!" : "Failed to send — is the app running?";
    urlInput.value = "";
  });
});

refreshStatus();
