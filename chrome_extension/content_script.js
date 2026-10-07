// Flux Downloader - Enhanced content script
// 1. Adds download button on YouTube videos
// 2. Adds IDM-style download icons on ALL video/audio elements

// ========================================
// YouTube Button (existing functionality)
// ========================================
function injectYouTubeButton() {
  if (document.getElementById("flux-dl-btn")) return;
  const target = document.querySelector("#above-the-fold #title, ytd-watch-metadata #title");
  if (!target) return;

  const btn = document.createElement("button");
  btn.id = "flux-dl-btn";
  btn.textContent = "⚡ Send to Flux Downloader";
  btn.style.cssText = `
    margin-top: 8px; padding: 6px 14px; border-radius: 8px; border: none;
    background: linear-gradient(#34aaff, #007AFF); color: white; font-weight: 600;
    cursor: pointer; font-family: -apple-system, sans-serif; font-size: 13px;
  `;
  btn.addEventListener("click", () => {
    chrome.runtime.sendMessage(
      { type: "FLUX_SEND_URL", url: window.location.href, referrer: document.title, kind: "youtube_video" },
      (resp) => {
        btn.textContent = resp && resp.ok ? "Sent ✓" : "Failed — app not running";
        setTimeout(() => (btn.textContent = "⚡ Send to Flux Downloader"), 2500);
      }
    );
  });
  target.parentElement.appendChild(btn);
}

// ========================================
// IDM-Style Download Icons on Video/Audio
// ========================================

const processedElements = new WeakSet();
let downloadIconsEnabled = true;

// Check if download icons are enabled in storage
chrome.storage.local.get(['downloadIconsEnabled'], (result) => {
  downloadIconsEnabled = result.downloadIconsEnabled !== false; // default true
  if (downloadIconsEnabled) {
    scanForMediaElements();
  }
});

function createDownloadIcon(mediaElement) {
  const icon = document.createElement('div');
  icon.className = 'flux-download-icon';
  icon.innerHTML = `
    <svg width="32" height="32" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg">
      <circle cx="16" cy="16" r="15" fill="#007AFF" stroke="white" stroke-width="2"/>
      <path d="M16 10V20M16 20L12 16M16 20L20 16" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
      <path d="M10 22H22" stroke="white" stroke-width="2" stroke-linecap="round"/>
    </svg>
  `;
  
  icon.style.cssText = `
    position: absolute;
    top: 10px;
    right: 10px;
    width: 32px;
    height: 32px;
    cursor: pointer;
    z-index: 999999;
    opacity: 0;
    transition: opacity 0.3s ease;
    background: rgba(0, 0, 0, 0.5);
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
  `;
  
  // Tooltip
  icon.title = "Download with Flux Downloader";
  
  // Click handler
  icon.addEventListener('click', (e) => {
    e.preventDefault();
    e.stopPropagation();
    downloadMedia(mediaElement);
  });
  
  return icon;
}

function downloadMedia(mediaElement) {
  const url = mediaElement.currentSrc || mediaElement.src;
  
  if (!url || url.startsWith('blob:') || url.startsWith('data:')) {
    showNotification('Cannot download this media (streaming/embedded)', 'error');
    return;
  }
  
  // Try to get filename from URL
  let filename = '';
  try {
    const urlObj = new URL(url);
    const pathname = urlObj.pathname;
    filename = pathname.split('/').pop() || 'media';
    
    // Add extension if missing
    if (!filename.includes('.')) {
      const type = mediaElement.tagName.toLowerCase();
      if (type === 'video') {
        filename += '.mp4';
      } else if (type === 'audio') {
        filename += '.mp3';
      }
    }
  } catch (e) {
    filename = mediaElement.tagName.toLowerCase() === 'video' ? 'video.mp4' : 'audio.mp3';
  }
  
  // Send to Flux Downloader
  chrome.runtime.sendMessage(
    {
      type: 'FLUX_SEND_URL',
      url: url,
      filename: filename,
      referrer: document.title,
      kind: mediaElement.tagName.toLowerCase()
    },
    (response) => {
      if (chrome.runtime.lastError) {
        showNotification('Flux Downloader not running', 'error');
      } else if (response && response.ok) {
        showNotification(`Downloading: ${filename}`, 'success');
      } else {
        showNotification('Failed to send to Flux Downloader', 'error');
      }
    }
  );
}

function showNotification(message, type = 'success') {
  // Create notification element
  const notification = document.createElement('div');
  notification.className = 'flux-notification';
  notification.textContent = message;
  
  const bgColor = type === 'success' ? '#4CAF50' : '#f44336';
  
  notification.style.cssText = `
    position: fixed;
    top: 20px;
    right: 20px;
    background: ${bgColor};
    color: white;
    padding: 12px 20px;
    border-radius: 8px;
    font-family: -apple-system, sans-serif;
    font-size: 14px;
    font-weight: 500;
    z-index: 9999999;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    animation: slideIn 0.3s ease;
  `;
  
  document.body.appendChild(notification);
  
  // Remove after 3 seconds
  setTimeout(() => {
    notification.style.animation = 'slideOut 0.3s ease';
    setTimeout(() => notification.remove(), 300);
  }, 3000);
}

function attachDownloadIcon(mediaElement) {
  if (processedElements.has(mediaElement)) return;
  processedElements.add(mediaElement);
  
  // Make parent position relative if not already
  const parent = mediaElement.parentElement;
  if (parent) {
    const parentStyle = window.getComputedStyle(parent);
    if (parentStyle.position === 'static') {
      parent.style.position = 'relative';
    }
  }
  
  // Create and attach icon
  const icon = createDownloadIcon(mediaElement);
  
  // Position relative to media element
  if (mediaElement.parentElement) {
    mediaElement.parentElement.style.position = 'relative';
    mediaElement.parentElement.appendChild(icon);
  }
  
  // Show icon on hover
  let hideTimeout;
  
  const showIcon = () => {
    clearTimeout(hideTimeout);
    icon.style.opacity = '1';
  };
  
  const hideIcon = () => {
    hideTimeout = setTimeout(() => {
      icon.style.opacity = '0';
    }, 500);
  };
  
  mediaElement.addEventListener('mouseenter', showIcon);
  mediaElement.addEventListener('mouseleave', hideIcon);
  icon.addEventListener('mouseenter', showIcon);
  icon.addEventListener('mouseleave', hideIcon);
  
  // Also show on play/pause
  mediaElement.addEventListener('play', () => {
    icon.style.opacity = '1';
    setTimeout(() => icon.style.opacity = '0', 2000);
  });
}

function scanForMediaElements() {
  if (!downloadIconsEnabled) return;
  
  // Find all video and audio elements
  const videos = document.querySelectorAll('video');
  const audios = document.querySelectorAll('audio');
  
  videos.forEach(video => {
    // Skip tiny videos (likely thumbnails)
    const rect = video.getBoundingClientRect();
    if (rect.width > 100 && rect.height > 100) {
      attachDownloadIcon(video);
    }
  });
  
  audios.forEach(audio => {
    attachDownloadIcon(audio);
  });
}

// Add CSS animations
const style = document.createElement('style');
style.textContent = `
  @keyframes slideIn {
    from {
      transform: translateX(400px);
      opacity: 0;
    }
    to {
      transform: translateX(0);
      opacity: 1;
    }
  }
  
  @keyframes slideOut {
    from {
      transform: translateX(0);
      opacity: 1;
    }
    to {
      transform: translateX(400px);
      opacity: 0;
    }
  }
  
  .flux-download-icon:hover {
    transform: scale(1.1);
  }
  
  .flux-download-icon:active {
    transform: scale(0.95);
  }
`;
document.head.appendChild(style);

// ========================================
// Observers and Initialization
// ========================================

// Observer for YouTube page changes
const youtubeObserver = new MutationObserver(() => {
  if (window.location.hostname.includes('youtube.com')) {
    injectYouTubeButton();
  }
});

// Observer for new media elements
const mediaObserver = new MutationObserver(() => {
  if (downloadIconsEnabled) {
    scanForMediaElements();
  }
});

// Start observing
youtubeObserver.observe(document.documentElement, { childList: true, subtree: true });
mediaObserver.observe(document.documentElement, { childList: true, subtree: true });

// Initial scan
if (window.location.hostname.includes('youtube.com')) {
  injectYouTubeButton();
}
scanForMediaElements();

// Rescan on page load
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    setTimeout(scanForMediaElements, 1000);
  });
} else {
  setTimeout(scanForMediaElements, 1000);
}

// Rescan periodically for dynamically loaded content
setInterval(scanForMediaElements, 3000);

// Listen for toggle messages from popup
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message.type === 'TOGGLE_DOWNLOAD_ICONS') {
    downloadIconsEnabled = message.enabled;
    
    if (!downloadIconsEnabled) {
      // Remove all download icons
      document.querySelectorAll('.flux-download-icon').forEach(icon => icon.remove());
    } else {
      // Rescan and add icons
      scanForMediaElements();
    }
    
    sendResponse({ ok: true });
  }
  return true;
});

console.log('Flux Downloader: Content script loaded - Video/Audio download icons enabled');
