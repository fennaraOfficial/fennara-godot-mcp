(function () {
  const DAEMON_WS_URL = "ws://127.0.0.1:41287/chat/ws";
  const PROMPT_MAX_HEIGHT = 126;
  const USER_COLLAPSE_CHARS = 700;
  const AUTO_SCROLL_THRESHOLD = 72;
  const DAEMON_RECONNECT_DELAY_MS = 250;
  const MAX_IMAGE_ATTACHMENTS = 4;
  const MAX_RAW_IMAGE_BYTES = 8 * 1024 * 1024;
  const MAX_SEND_IMAGE_BYTES = 3 * 1024 * 1024;
  const MAX_TOTAL_IMAGE_BYTES = 20 * 1024 * 1024;
  const SHOW_RELOAD_BUTTON = true;
  const DEFAULT_OLLAMA_BASE_URL = "http://127.0.0.1:11434";
  const DEFAULT_LOCAL_BASE_URLS = {
    ollama: DEFAULT_OLLAMA_BASE_URL,
    lmstudio: "http://127.0.0.1:1234/v1",
  };
  const CHAT_SURFACE_EMBEDDED = "embedded";
  const CHAT_SURFACE_BROWSER = "browser";
  const RUNTIME_CHAT_SURFACE = /^https?:$/.test(window.location.protocol)
    ? CHAT_SURFACE_BROWSER
    : CHAT_SURFACE_EMBEDDED;
  const SUPPORTED_IMAGE_TYPES = new Set(["image/png", "image/jpeg", "image/webp", "image/gif"]);
  const COPY_ICON = '<svg class="svg-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M6 11c0-2.83 0-4.24.88-5.12C7.76 5 9.17 5 12 5h3c2.83 0 4.24 0 5.12.88C21 6.76 21 8.17 21 11v5c0 2.83 0 4.24-.88 5.12C19.24 22 17.83 22 15 22h-3c-2.83 0-4.24 0-5.12-.88C6 20.24 6 18.83 6 16v-5Z"></path><path d="M6 19a3 3 0 0 1-3-3v-6c0-3.77 0-5.66 1.17-6.83C5.34 2 7.23 2 11 2h4a3 3 0 0 1 3 3"></path></svg>';
  const CHECK_ICON = '<svg class="svg-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="m20 6-11 11-5-5"></path></svg>';
  const settingsDialog = document.querySelector("[data-settings]");
  const commandPopover = document.querySelector("[data-command-popover]");
  const commandOptionButtons = Array.from(document.querySelectorAll("[data-command-option]"));
  const modelPopover = document.querySelector("[data-model-popover]");
  const providerPopover = document.querySelector("[data-provider-popover]");
  const providerKeyPopover = document.querySelector("[data-provider-key-popover]");
  const ollamaSetupPopover = document.querySelector("[data-ollama-setup-popover]");
  const modelTrigger = document.querySelector("[data-open-model-picker]");
  const modelSearch = document.querySelector("[data-model-search]");
  const modelList = document.querySelector("[data-model-list]");
  const modelDetail = document.querySelector("[data-model-detail]");
  const providerStatuses = document.querySelectorAll("[data-provider-status]");
  const providerDot = document.querySelector(".composer-model-dot");
  const providerOptionsList = document.querySelector("[data-provider-options]");
  const providerSearch = document.querySelector("[data-provider-search]");
  const ollamaForm = document.querySelector("[data-ollama-form]");
  const ollamaBaseUrlInput = document.querySelector("[data-ollama-base-url]");
  const localSetupTitle = document.querySelector("[data-local-setup-title]");
  const localSetupHelp = document.querySelector("[data-local-setup-help]");
  const providerKeyForm = document.querySelector("[data-provider-key-form]");
  const providerKeyTitle = document.querySelector("[data-provider-key-title]");
  const providerKeyInlineInput = document.querySelector("[data-provider-key-inline]");
  const transcript = document.querySelector("[data-transcript]");
  const chatList = document.querySelector("[data-chat-list]");
  const chatTitle = document.querySelector("[data-chat-title]");
  const composer = document.querySelector("[data-composer]");
  const prompt = document.querySelector("[data-prompt]");
  const attachImageButton = document.querySelector("[data-attach-image]");
  const imageInput = document.querySelector("[data-image-input]");
  const attachmentPreview = document.querySelector("[data-attachment-preview]");
  const chatSurfaceBrowserInput = document.querySelector("[data-chat-surface-browser]");
  const chatSurfaceRestartStatus = document.querySelector("[data-chat-surface-restart]");
  const modelInput = document.querySelector("[data-model]");
  const modelStatuses = document.querySelectorAll("[data-model-status]");
  const chatSizeStatus = document.querySelector("[data-chat-size]");
  const sessionCostStatus = document.querySelector("[data-session-cost]");
  const setMcpTargetButton = document.querySelector("[data-set-mcp-target]");
  const targetPillText = document.querySelector("[data-target-pill-text]");
  const targetMenu = document.querySelector("[data-target-menu]");
  const targetPopoverTitle = document.querySelector("[data-target-popover-title]");
  const targetPopoverText = document.querySelector("[data-target-popover-text]");
  const versionMenu = document.querySelector("[data-version-menu]");
  const versionWarning = document.querySelector("[data-version-warning]");
  const versionPopover = document.querySelector("[data-version-popover]");
  const versionWarningText = document.querySelector("[data-version-warning-text]");
  const versionCommand = document.querySelector("[data-version-command]");
  const usageContainer = document.querySelector(".composer-usage");
  const usagePopover = document.querySelector("[data-usage-popover]");
  const usageTotalCost = document.querySelector("[data-usage-total-cost]");
  const usageContextStatus = document.querySelector("[data-usage-context]");
  const reasoningEffortControls = document.querySelectorAll("[data-reasoning-effort]");
  const effortStatus = document.querySelector("[data-effort-status]");
  const effortToggle = document.querySelector("[data-effort-toggle]");
  const effortOptions = document.querySelector("[data-effort-options]");
  const effortOptionButtons = document.querySelectorAll("[data-effort-option]");
  const sendButton = document.querySelector("[data-send-button]");
  const revertButton = document.querySelector("[data-revert-button]");
  const saveSettingsButton = document.querySelector("[data-save-settings]");
  const reloadButton = document.querySelector("[data-reload-ui]");
  const appShell = document.querySelector(".app-shell");
  const markdown = window.markdownit({
    html: false,
    linkify: true,
    typographer: true,
    breaks: false,
  });

  if (window.markdownitTaskLists) {
    markdown.use(window.markdownitTaskLists, { enabled: false, label: false });
  }

  markdown.renderer.rules.fence = function (tokens, index, options, env, self) {
    const token = tokens[index];
    const language = token.info.trim().split(/\s+/)[0] || "text";
    const code = token.content;
    const escapedLanguage = markdown.utils.escapeHtml(language);
    const escapedCode = markdown.utils.escapeHtml(code);
    return [
      '<figure class="code-block">',
      "<figcaption>",
      `<span>${escapedLanguage}</span>`,
      '<button class="copy-code-button" type="button" aria-label="Copy code" data-code-copy>',
      COPY_ICON,
      "</button>",
      "</figcaption>",
      `<pre><code>${escapedCode}</code></pre>`,
      "</figure>",
    ].join("");
  };

  let socket = null;
  let daemonConnected = false;
  let reconnectTimer = 0;
  let requestCounter = 0;
  let activeChatId = null;
  let currentModel = "";
  let currentProvider = "";
  let currentReasoningEffort = "medium";
  let currentChatSurface = CHAT_SURFACE_EMBEDDED;
  let hasOpenRouterKey = false;
  let hasOllamaCloudKey = false;
  let providerRegistry = [];
  let providerMetadata = new Map();
  let keyPromptProvider = "";
  let ollamaBaseUrl = DEFAULT_OLLAMA_BASE_URL;
  let providerBaseUrls = new Map(Object.entries(DEFAULT_LOCAL_BASE_URLS));
  let ollamaModels = [];
  let ollamaStatus = "unknown";
  let localProviderStatuses = new Map();
  let lastModelCatalog = null;
  let openrouterCatalogStatus = null;
  let catalogRefreshInFlight = false;
  let chatStreaming = false;
  let sessionCost = 0;
  let activeTurnCost = 0;
  const optimisticUserMessageRequests = new Map();
  let latestPromptTokens = 0;
  let projectStatusTimer = 0;
  let usageCloseTimer = 0;
  let canRevert = false;
  let modelPicker = null;
  let pendingSettingsPayload = null;
  let attachedImages = [];
  let activeCommandIndex = 0;
  let activeCommandRange = null;
  let lastCommandQuery = "";
  let visibleCommandList = [];
  const transcriptRenderer = window.FennaraTranscriptRenderer.createTranscriptRenderer({
    transcript,
    markdown,
    copyIcon: COPY_ICON,
    checkIcon: CHECK_ICON,
    userCollapseChars: USER_COLLAPSE_CHARS,
    autoScrollThreshold: AUTO_SCROLL_THRESHOLD,
  });

  modelPicker = window.FennaraModelPicker?.createModelPicker({
    popover: modelPopover,
    trigger: modelTrigger,
    search: modelSearch,
    list: modelList,
    detail: modelDetail,
    getCurrentModel: () => currentModel,
    getCurrentProvider: () => currentProvider,
    getProviders: () => providerRegistry,
    isProviderConnected: providerConnected,
    providerFromModel,
    hasOpenRouterKey: () => hasOpenRouterKey,
    hasOllamaCloudKey: () => hasOllamaCloudKey,
    getOllamaModels: () => ollamaModels,
    openProviderPicker,
    openOpenRouterKeyPrompt,
    openProviderKeyPrompt,
    onSelect: selectModel,
    onEscapeClose: focusComposer,
    onRequestModels: () => requestModelList({ refreshOllama: true }),
    onRefreshCatalog: () => refreshModelCatalog(true),
  });

  function daemonWsUrl() {
    if (/^https?:$/.test(window.location.protocol) && /^(127\.0\.0\.1|localhost)$/.test(window.location.hostname)) {
      const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      return `${protocol}//${window.location.host}/chat/ws`;
    }
    return DAEMON_WS_URL;
  }

  function chatWsUrl() {
    const params = new URLSearchParams(window.location.search);
    const token = params.get("chat_token") || params.get("session") || "";
    const baseUrl = daemonWsUrl();
    return token ? baseUrl + "?chat_token=" + encodeURIComponent(token) : baseUrl;
  }

  function nextRequestId(prefix) {
    requestCounter += 1;
    return prefix + "-" + Date.now() + "-" + requestCounter;
  }

  function connect() {
    window.clearTimeout(reconnectTimer);
    socket = new WebSocket(chatWsUrl());

    socket.addEventListener("open", () => {
      daemonConnected = true;
      appShell?.setAttribute("data-connection", "online");
      send({ type: "get_settings", request_id: nextRequestId("settings") });
      requestProjectStatus();
      startProjectStatusPolling();
      requestModelList();
      flushPendingSettings();
    });

    socket.addEventListener("message", (event) => {
      let message = null;
      try {
        message = JSON.parse(event.data);
      } catch {
        return;
      }
      handleDaemonMessage(message);
    });

    socket.addEventListener("close", () => {
      daemonConnected = false;
      appShell?.setAttribute("data-connection", "offline");
      stopProjectStatusPolling();
      reconnectTimer = window.setTimeout(connect, DAEMON_RECONNECT_DELAY_MS);
    });
  }

  function send(payload) {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      appendSystem("Local daemon is not connected yet.");
      return false;
    }
    socket.send(JSON.stringify(payload));
    return true;
  }

  function ensureDaemonConnected() {
    if (daemonConnected && socket?.readyState === WebSocket.OPEN) {
      return true;
    }
    appendSystem("Connecting to local daemon...");
    if (!socket || socket.readyState === WebSocket.CLOSED || socket.readyState === WebSocket.CLOSING) {
      connect();
    }
    return false;
  }

  function requestModelList(options = {}) {
    if (options.refreshOllama) {
      ollamaStatus = "checking";
      ollamaModels = [];
      providerRegistry
        .filter((provider) => provider.kind === "local")
        .forEach((provider) => {
          const existing = localProviderStatuses.get(provider.id) || {};
          localProviderStatuses.set(provider.id, { ...existing, state: "checking" });
        });
      updateProviderUi();
      updateModelUi();
    }
    return send({ type: "list_models", request_id: nextRequestId("list-models") });
  }

  function refreshModelCatalog(force = true) {
    catalogRefreshInFlight = true;
    modelPicker?.applyCatalog({
      ...(lastModelCatalog || {}),
      catalog_status: openrouterCatalogStatus,
      refreshing: true,
      providers: providerRegistry,
    });
    const sent = send({
      type: "refresh_model_catalog",
      request_id: nextRequestId("refresh-model-catalog"),
      force,
    });
    if (!sent) {
      catalogRefreshInFlight = false;
    }
    return sent;
  }

  function requestProjectStatus() {
    return send({ type: "get_project_status", request_id: nextRequestId("project-status") });
  }

  function startProjectStatusPolling() {
    stopProjectStatusPolling();
    projectStatusTimer = window.setInterval(requestProjectStatus, 5000);
  }

  function stopProjectStatusPolling() {
    window.clearInterval(projectStatusTimer);
    projectStatusTimer = 0;
  }

  function setStreaming(nextStreaming) {
    chatStreaming = nextStreaming;
    appShell?.classList.toggle("is-streaming", nextStreaming);
    if (sendButton) {
      sendButton.setAttribute("aria-busy", String(nextStreaming));
      sendButton.querySelector(".send-label").textContent = nextStreaming ? "Cancel" : "Send";
    }
    updateRevertButton();
  }

  function openSettings() {
    if (!ensureDaemonConnected()) {
      return;
    }
    setUsagePopoverOpen(false);
    closeProviderPicker();
    closeOpenRouterKeyPrompt();
    closeOllamaSetupPrompt();
    closeCommandPalette();
    if (chatSurfaceBrowserInput) {
      chatSurfaceBrowserInput.checked = currentChatSurface === CHAT_SURFACE_BROWSER;
    }
    updateChatSurfaceRestartNotice(currentChatSurface);
    if (settingsDialog && typeof settingsDialog.showModal === "function") {
      settingsDialog.showModal();
    }
  }

  function openModelPicker(forceOpen = false) {
    if (!ensureDaemonConnected()) {
      return;
    }
    setUsagePopoverOpen(false);
    closeProviderPicker();
    closeOpenRouterKeyPrompt();
    closeOllamaSetupPrompt();
    closeCommandPalette();
    if (forceOpen && modelPicker?.open()) {
      return;
    }
    if (!modelPicker?.toggle()) {
      openProviderPicker();
    }
  }

  function openProviderPicker() {
    if (!ensureDaemonConnected()) {
      return false;
    }
    setUsagePopoverOpen(false);
    modelPicker?.close();
    closeOpenRouterKeyPrompt();
    closeOllamaSetupPrompt();
    closeCommandPalette();
    if (!providerPopover) {
      return false;
    }
    providerPopover.setAttribute("tabindex", "-1");
    providerPopover.hidden = false;
    providerPopover.focus({ preventScroll: true });
    requestModelList({ refreshOllama: true });
    renderProviderOptions();
    positionProviderPopover();
    window.setTimeout(() => providerSearch?.focus({ preventScroll: true }), 0);
    return true;
  }

  function closeProviderPicker() {
    if (!providerPopover || providerPopover.hidden) {
      return;
    }
    providerPopover.hidden = true;
  }

  function centerPopover(popover, width = 560) {
    if (!popover) {
      return;
    }
    const viewportPad = 10;
    const nextWidth = Math.min(width, Math.max(300, window.innerWidth - viewportPad * 2));
    const height = popover.offsetHeight || 180;
    popover.style.width = nextWidth + "px";
    popover.style.left = Math.max(viewportPad, (window.innerWidth - nextWidth) / 2) + "px";
    popover.style.top = Math.max(viewportPad, (window.innerHeight - height) / 2) + "px";
    popover.dataset.side = "center";
  }

  function positionProviderPopover() {
    centerPopover(providerPopover, 560);
  }

  function openOpenRouterKeyPrompt() {
    return openProviderKeyPrompt("openrouter");
  }

  function openProviderKeyPrompt(providerId) {
    if (!ensureDaemonConnected()) {
      return false;
    }
    const provider = providerMetadata.get(providerId) || providerMetadata.get("openrouter");
    setUsagePopoverOpen(false);
    modelPicker?.close();
    closeProviderPicker();
    closeOllamaSetupPrompt();
    closeCommandPalette();
    if (!providerKeyPopover || !provider) {
      return false;
    }
    keyPromptProvider = provider.id;
    if (providerKeyTitle) {
      providerKeyTitle.textContent = `${provider.name} API key`;
    }
    if (providerKeyInlineInput) {
      providerKeyInlineInput.value = "";
      providerKeyInlineInput.placeholder = provider.auth?.env || "API key";
    }
    providerKeyPopover.setAttribute("tabindex", "-1");
    providerKeyPopover.hidden = false;
    providerKeyPopover.focus({ preventScroll: true });
    positionProviderKeyPrompt();
    window.setTimeout(() => providerKeyInlineInput?.focus(), 0);
    return true;
  }

  function closeOpenRouterKeyPrompt() {
    if (!providerKeyPopover || providerKeyPopover.hidden) {
      return;
    }
    providerKeyPopover.hidden = true;
  }

  function openOllamaSetupPrompt() {
    if (!ensureDaemonConnected()) {
      return false;
    }
    setUsagePopoverOpen(false);
    modelPicker?.close();
    closeProviderPicker();
    closeOpenRouterKeyPrompt();
    closeCommandPalette();
    if (!ollamaSetupPopover) {
      return false;
    }
    syncOllamaSetupFields();
    ollamaSetupPopover.setAttribute("tabindex", "-1");
    ollamaSetupPopover.hidden = false;
    ollamaSetupPopover.focus({ preventScroll: true });
    positionOllamaSetupPrompt();
    window.setTimeout(() => ollamaBaseUrlInput?.focus({ preventScroll: true }), 0);
    return true;
  }

  function closeOllamaSetupPrompt() {
    if (!ollamaSetupPopover || ollamaSetupPopover.hidden) {
      return;
    }
    ollamaSetupPopover.hidden = true;
  }

  function focusComposer() {
    if (!prompt || chatStreaming) {
      return;
    }
    const restore = () => {
      prompt.focus({ preventScroll: true });
      const end = prompt.value.length;
      prompt.setSelectionRange?.(end, end);
    };
    window.setTimeout(restore, 0);
    window.requestAnimationFrame?.(() => window.setTimeout(restore, 0));
  }

  function positionProviderKeyPrompt() {
    centerPopover(providerKeyPopover, 420);
  }

  function positionOllamaSetupPrompt() {
    centerPopover(ollamaSetupPopover, 420);
  }

  function renderProviderOptions() {
    const query = String(providerSearch?.value || "").trim().toLowerCase();
    if (!providerOptionsList) {
      return;
    }
    const ranked = providerRegistry
      .map((provider, index) => ({
        provider,
        index,
        label: `${provider.name} ${provider.id}`.toLowerCase(),
      }))
      .map((item) => ({ ...item, score: providerScore(item.label, query) }))
      .filter((item) => item.score < 99)
      .sort((a, b) => a.score - b.score || a.index - b.index);
    providerOptionsList.replaceChildren();
    ranked.forEach(({ provider }) => {
      providerOptionsList.append(renderProviderRow(provider));
    });
    if (!ranked.length) {
      const empty = document.createElement("div");
      empty.className = "model-empty";
      const text = document.createElement("p");
      text.textContent = "No matching providers.";
      empty.append(text);
      providerOptionsList.append(empty);
    }
    positionProviderPopover();
  }

  function renderProviderRow(provider) {
    const row = document.createElement("button");
    row.className = "provider-row";
    row.type = "button";
    row.dataset.providerOption = provider.id;
    row.innerHTML = [
      "<span>",
      `<strong>${escapeHtml(provider.name)}</strong>`,
      "</span>",
      `<b>${escapeHtml(providerStatusLabel(provider))}</b>`,
    ].join("");
    row.addEventListener("click", (event) => {
      event.stopPropagation();
      chooseProvider(provider.id);
    });
    return row;
  }

  function syncOllamaSetupFields() {
    const provider = providerMetadata.get(currentProvider) || providerMetadata.get("ollama");
    const defaultBaseUrl = provider?.setup?.default_base_url || DEFAULT_LOCAL_BASE_URLS[provider?.id] || DEFAULT_OLLAMA_BASE_URL;
    const baseUrl = providerBaseUrl(provider?.id || "ollama");
    if (localSetupTitle) {
      localSetupTitle.textContent = provider?.name || "Local provider";
    }
    if (localSetupHelp) {
      localSetupHelp.textContent = `${provider?.name || "This provider"} uses ${defaultBaseUrl} by default. Override it only if your local server uses another URL. Models are listed automatically from the local server.`;
    }
    if (ollamaBaseUrlInput) {
      ollamaBaseUrlInput.placeholder = defaultBaseUrl;
      ollamaBaseUrlInput.value = baseUrl === defaultBaseUrl ? "" : baseUrl;
    }
  }

  function providerScore(label, query) {
    if (!query) {
      return 0;
    }
    if (label === query) {
      return 0;
    }
    if (label.startsWith(query)) {
      return 1;
    }
    if (label.split(/[\\s()_-]+/).some((part) => part.startsWith(query))) {
      return 2;
    }
    if (label.includes(query)) {
      return 3;
    }
    return 99;
  }

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function commandMatch() {
    const value = prompt?.value || "";
    const cursor = prompt?.selectionStart ?? value.length;
    const beforeCursor = value.slice(0, cursor);
    const match = beforeCursor.match(/(^|\s)(\/[^\s]*)$/);
    if (!match) {
      return null;
    }
    const token = match[2] || "";
    return {
      query: token.toLowerCase(),
      start: cursor - token.length,
      end: cursor,
    };
  }

  function commandQuery() {
    const match = commandMatch();
    activeCommandRange = match ? { start: match.start, end: match.end } : null;
    return match?.query || "";
  }

  function openCommandPalette() {
    if (!commandPopover || !prompt) {
      return false;
    }
    commandPopover.hidden = false;
    renderCommandPalette();
    positionCommandPalette();
    return true;
  }

  function closeCommandPalette() {
    if (!commandPopover || commandPopover.hidden) {
      return;
    }
    commandPopover.hidden = true;
    activeCommandIndex = 0;
    visibleCommandList = [];
    lastCommandQuery = "";
  }

  function updateCommandPalette() {
    if (!prompt || !commandPopover) {
      return;
    }
    if (commandQuery()) {
      openCommandPalette();
    } else {
      closeCommandPalette();
    }
  }

  function renderCommandPalette() {
    const query = commandQuery();
    if (query !== lastCommandQuery) {
      activeCommandIndex = 0;
      lastCommandQuery = query;
    }
    const ranked = commandOptionButtons
      .map((button, index) => ({
        button,
        index,
        command: String(button.dataset.commandOption || "").toLowerCase(),
        title: String(button.querySelector("span")?.textContent || "").toLowerCase(),
      }))
      .map((item) => ({
        ...item,
        score: commandScore(item.command, item.title, query),
      }))
      .filter((item) => item.score < 99)
      .sort((a, b) => a.score - b.score || a.index - b.index);
    visibleCommandList = ranked.map((item) => item.button);
    const palette = commandPopover?.querySelector(".command-palette");
    visibleCommandList.forEach((button) => {
      button.hidden = false;
      palette?.appendChild(button);
    });
    commandOptionButtons.forEach((button) => {
      if (!visibleCommandList.includes(button)) {
        button.hidden = true;
      }
    });
    if (activeCommandIndex >= visibleCommandList.length) {
      activeCommandIndex = 0;
    }
    visibleCommandList.forEach((button, index) => {
      button.setAttribute("aria-selected", String(index === activeCommandIndex));
    });
    commandPopover.hidden = visibleCommandList.length === 0;
  }

  function commandScore(command, title, query) {
    if (!query) {
      return 99;
    }
    const needle = query.replace(/^\//, "");
    const trigger = command.replace(/^\//, "");
    if (!needle) {
      return 0;
    }
    if (trigger === needle) {
      return 0;
    }
    if (trigger.startsWith(needle)) {
      return 1;
    }
    if (title.startsWith(needle)) {
      return 2;
    }
    if (trigger.includes(needle) || title.includes(needle)) {
      return 3;
    }
    return 99;
  }

  function positionCommandPalette() {
    if (!commandPopover) {
      return;
    }
    const anchor = document.querySelector(".composer-card") || prompt;
    if (!anchor) {
      return;
    }
    const gap = 8;
    const margin = 10;
    const anchorRect = anchor.getBoundingClientRect();
    const width = Math.min(360, Math.max(280, window.innerWidth - margin * 2));
    const height = commandPopover.offsetHeight || 150;
    const left = Math.min(
      Math.max(margin, anchorRect.left),
      window.innerWidth - width - margin,
    );
    const top = Math.max(margin, anchorRect.top - gap - height);
    commandPopover.style.width = width + "px";
    commandPopover.style.left = left + "px";
    commandPopover.style.top = top + "px";
  }

  function visibleCommandButtons() {
    return visibleCommandList.filter((button) => !button.hidden);
  }

  function moveCommandSelection(delta) {
    const buttons = visibleCommandButtons();
    if (!buttons.length) {
      return;
    }
    activeCommandIndex = (activeCommandIndex + delta + buttons.length) % buttons.length;
    renderCommandPalette();
  }

  function runCommand(command) {
    const clean = String(command || "").trim().toLowerCase();
    if (!clean) {
      return;
    }
    if ((clean === "/provider" || clean === "/model") && !ensureDaemonConnected()) {
      return;
    }
    if (prompt && activeCommandRange) {
      const value = prompt.value || "";
      prompt.value = value.slice(0, activeCommandRange.start) + value.slice(activeCommandRange.end);
      prompt.selectionStart = activeCommandRange.start;
      prompt.selectionEnd = activeCommandRange.start;
      resizePrompt();
    }
    activeCommandRange = null;
    closeCommandPalette();
    if (clean === "/provider") {
      openProviderPicker();
    } else if (clean === "/model") {
      openModelPicker(true);
    }
  }

  function reloadUi() {
    const nextUrl = new URL(window.location.href);
    nextUrl.searchParams.set("v", String(Date.now()));
    window.location.replace(nextUrl.toString());
  }

  function clearTranscript(resetCost = true) {
    transcriptRenderer.clear(resetCost, () => {
      sessionCost = 0;
      latestPromptTokens = 0;
      updateChatSize();
      updateSessionCost();
    });
    canRevert = false;
    updateRevertButton();
  }

  function appendMessage(role, text, attachments = []) {
    return transcriptRenderer.appendMessage(role, text, attachments);
  }

  function appendDaemonUserMessage(userMessage, requestId = "") {
    const node = appendMessage(
      "user",
      userMessage.content || "",
      imagesFromMetadata(userMessage.metadata_json),
    );
    const optimistic = optimisticUserMessageRequests.get(String(requestId || ""));
    if (optimistic) {
      optimistic.node = node;
    }
    return node;
  }

  function hasConnectedOptimisticUserMessage(requestId) {
    const optimistic = optimisticUserMessageRequests.get(String(requestId || ""));
    return Boolean(optimistic?.node?.isConnected);
  }

  function restorePendingOptimisticUserMessages(chatId) {
    for (const optimistic of optimisticUserMessageRequests.values()) {
      if (optimistic.node?.isConnected) {
        continue;
      }
      if (optimistic.chatId && chatId && optimistic.chatId !== chatId) {
        continue;
      }
      optimistic.node = appendMessage("user", optimistic.text || "", optimistic.images || []);
    }
  }

  function renderStoredMessages(messages) {
    clearTranscript(false);
    let pendingHiddenAssistantCost = 0;
    let storedPromptTokens = 0;
    for (const message of messages || []) {
      const storedUsage = parseUsage(message.usage_json);
      const promptTokens = usagePromptTokens(storedUsage);
      if (promptTokens > 0) {
        storedPromptTokens = promptTokens;
      }
      if (message.role === "assistant" && message.reasoning_content) {
        appendStoredThinking(message.reasoning_content);
      }
      if (message.role === "tool") {
        appendStoredTool(message);
        continue;
      }
      if (isStoredToolCallAssistant(message)) {
        pendingHiddenAssistantCost += storedMessageCost(message);
        continue;
      }
      const node = appendMessage(message.role, message.content || "", imagesFromMetadata(message.metadata_json));
      if (message.role === "assistant" && shouldShowStoredAssistantActions(message)) {
        const usage = parseUsage(message.usage_json) || { cost: message.cost };
        const visibleCost = usageCost(usage);
        const combinedCost = pendingHiddenAssistantCost + (Number.isFinite(visibleCost) ? visibleCost : 0);
        transcriptRenderer.addActionsToMessage(
          node,
          combinedCost > 0 ? { ...usage, cost: combinedCost } : usage,
          formatUsageCost,
        );
        pendingHiddenAssistantCost = 0;
      }
    }
    if (storedPromptTokens > 0) {
      latestPromptTokens = storedPromptTokens;
      updateChatSize();
    }
  }

  function isStoredToolCallAssistant(message) {
    return message.role === "assistant" &&
      !(message.content || "").trim() &&
      Boolean(message.tool_calls_json);
  }

  function shouldShowStoredAssistantActions(message) {
    return Boolean((message.content || "").trim()) || usageCost(parseUsage(message.usage_json)) > 0 || Number(message.cost) > 0;
  }

  function storedMessageCost(message) {
    const usage = parseUsage(message.usage_json);
    const cost = usageCost(usage) || Number(message.cost);
    return Number.isFinite(cost) && cost > 0 ? cost : 0;
  }

  function appendStoredTool(message) {
    const id = message.tool_call_id || message.id;
    const name = message.tool_name || "tool";
    const status = message.status === "failed" ? "failed" : "done";
    updateToolCall({
      id,
      name,
      status,
      content: message.content || "",
    });
  }

  function imagesFromMetadata(raw) {
    if (!raw) {
      return [];
    }
    try {
      const metadata = typeof raw === "string" ? JSON.parse(raw) : raw;
      const images = Array.isArray(metadata?.images) ? metadata.images : [];
      return images.filter((image) =>
        image &&
        SUPPORTED_IMAGE_TYPES.has(String(image.mime_type || "").toLowerCase()) &&
        typeof image.base64 === "string" &&
        image.base64.length > 0
      );
    } catch {
      return [];
    }
  }

  function appendStoredThinking(text) {
    transcriptRenderer.appendStoredThinking(text);
  }

  function parseUsage(raw) {
    if (!raw) {
      return null;
    }
    try {
      return JSON.parse(raw);
    } catch {
      return null;
    }
  }

  function appendSystem(text) {
    transcriptRenderer.appendSystem(text);
  }

  function clearSystemStatus() {
    transcriptRenderer.clearSystemStatus();
  }

  function updateThinkingText(text, status) {
    transcriptRenderer.updateThinkingText(text, status);
  }

  function updateAssistantText(text) {
    if (String(text || "").trim()) {
      transcriptRenderer.finishActiveThinking();
    }
    transcriptRenderer.updateAssistantText(text);
  }

  function updateToolCall(item) {
    transcriptRenderer.updateToolCall(item);
  }

  function flashCopied(button, normalLabel, copiedLabel) {
    transcriptRenderer.flashCopied(button, normalLabel, copiedLabel);
  }

  function formatUsageCost(usage) {
    const cost = usageCost(usage);
    if (!Number.isFinite(cost) || cost <= 0) {
      return "";
    }
    return formatCostValue(cost);
  }

  function usageCost(usage) {
    const rawCost = usage?.cost;
    return Number(rawCost);
  }

  function usagePromptTokens(usage) {
    const value =
      usage?.prompt_tokens ?? usage?.promptTokens ?? usage?.total_tokens ?? usage?.totalTokens;
    const tokens = Number(value);
    return Number.isFinite(tokens) && tokens > 0 ? tokens : 0;
  }

  function formatTokenCount(value) {
    const tokens = Number(value);
    if (!Number.isFinite(tokens) || tokens <= 0) {
      return "0";
    }
    if (tokens < 1000) {
      return String(Math.round(tokens));
    }
    if (tokens < 1000000) {
      return (tokens / 1000).toFixed(tokens < 10000 ? 1 : 0).replace(/\.0$/, "") + "k";
    }
    return (tokens / 1000000).toFixed(tokens < 10000000 ? 1 : 0).replace(/\.0$/, "") + "M";
  }

  function updateChatSize() {
    const availableTokens = Number(modelPicker?.modelInfo(currentModel)?.context_length || 0);
    const hasAvailable = Number.isFinite(availableTokens) && availableTokens > 0;
    if (chatSizeStatus) {
      const usedText = formatTokenCount(latestPromptTokens);
      const availableText = hasAvailable ? formatTokenCount(availableTokens) : "?";
      chatSizeStatus.textContent = `*${usedText} / ${availableText} tokens`;
    }
    if (usageContextStatus) {
      usageContextStatus.textContent = hasAvailable ? `*${formatTokenCount(availableTokens)} tokens` : "*Unknown";
    }
  }

  function updateSessionCost() {
    if (!sessionCostStatus) {
      return;
    }
    sessionCostStatus.hidden = sessionCost <= 0;
    sessionCostStatus.textContent = sessionCost > 0 ? "*" + formatCostValue(sessionCost) : "";
    sessionCostStatus.title = "";
    if (usageTotalCost) {
      usageTotalCost.textContent = sessionCost > 0 ? "*" + formatCostValue(sessionCost) : "*$0.00";
    }
    if (sessionCostStatus.hidden) {
      setUsagePopoverOpen(false);
    }
  }

  function positionUsagePopover() {
    if (!usagePopover || !sessionCostStatus || usagePopover.hidden) {
      return;
    }
    const margin = 12;
    const gap = 10;
    const anchor = sessionCostStatus.getBoundingClientRect();
    const width = usagePopover.offsetWidth;
    const height = usagePopover.offsetHeight;
    const maxLeft = Math.max(margin, window.innerWidth - width - margin);
    let left = anchor.left + anchor.width / 2 - width / 2;
    left = Math.min(Math.max(left, margin), maxLeft);
    let top = anchor.top - height - gap;
    if (top < margin) {
      top = Math.min(window.innerHeight - height - margin, anchor.bottom + gap);
    }
    usagePopover.style.setProperty("--usage-popover-left", `${Math.max(margin, left)}px`);
    usagePopover.style.setProperty("--usage-popover-top", `${Math.max(margin, top)}px`);
  }

  function setUsagePopoverOpen(open) {
    if (!usagePopover || !sessionCostStatus) {
      return;
    }
    const shouldOpen = Boolean(open) && !sessionCostStatus.hidden;
    usagePopover.hidden = !shouldOpen;
    sessionCostStatus.setAttribute("aria-expanded", shouldOpen ? "true" : "false");
    if (shouldOpen) {
      positionUsagePopover();
    }
  }

  function basename(path) {
    return String(path || "").split(/[\\/]/).filter(Boolean).pop() || "";
  }

  function applyProjectStatus(message) {
    const daemon = message.daemon || {};
    const boundSessionId = message.bound_session_id || "";
    const connectedProjects = Array.isArray(daemon.connected_projects) ? daemon.connected_projects : [];
    const boundProject =
      connectedProjects.find((project) => project.session_id === boundSessionId) ||
      daemon.active_project ||
      {};
    const activeProject = daemon.active_project || null;
    const isTarget = Boolean(daemon.active_session_id && daemon.active_session_id === boundSessionId);
    const targetName = activeProject?.project_name || basename(activeProject?.project_path) || "No MCP target";
    const boundName = boundProject?.project_name || basename(boundProject?.project_path) || "Godot project";
    const boundPath = boundProject?.project_path || "";

    if (targetMenu) {
      targetMenu.hidden = false;
    }
    if (setMcpTargetButton) {
      setMcpTargetButton.classList.toggle("is-target", isTarget);
      setMcpTargetButton.classList.remove("is-setting");
      setMcpTargetButton.classList.toggle("has-other-target", Boolean(activeProject) && !isTarget);
    }
    if (targetPillText) {
      targetPillText.textContent = isTarget ? "MCP target" : "Use for MCP";
    }
    if (targetPopoverTitle && targetPopoverText) {
      targetPopoverTitle.textContent = isTarget ? `${boundName} is the MCP target` : "Use this project for MCP";
      targetPopoverText.textContent = isTarget
        ? "External MCP clients send Godot tool calls here."
        : activeProject
          ? `Current target: ${targetName}. Click to switch MCP to this project.`
          : "No target is selected. Click to use this project.";
    }

    applyVersionWarning(message.version || {});
  }

  function applyVersionWarning(version) {
    const outdated = Boolean(version.outdated);
    if (!versionMenu || !versionWarning) {
      return;
    }
    versionMenu.hidden = !outdated;
    versionWarning.setAttribute("aria-expanded", "false");
    if (!outdated) {
      return;
    }
    const current = version.current_version || "installed";
    const latest = version.latest_version || "latest";
    if (versionWarningText) {
      versionWarningText.innerHTML = [
        `Current: ${markdown.utils.escapeHtml(current)}`,
        `Available: ${markdown.utils.escapeHtml(latest)}`,
        "",
        "Close Godot, then run this in the current project.",
      ].join("<br>");
    }
    if (versionCommand) {
      versionCommand.textContent = "fennara update";
    }
    if (versionPopover) {
      versionPopover.hidden = false;
    }
  }

  function showUsagePopover() {
    window.clearTimeout(usageCloseTimer);
    setUsagePopoverOpen(true);
  }

  function hideUsagePopoverSoon() {
    window.clearTimeout(usageCloseTimer);
    usageCloseTimer = window.setTimeout(() => setUsagePopoverOpen(false), 90);
  }

  function updateChatTitle(chat) {
    if (!chatTitle) {
      return;
    }
    chatTitle.textContent = chat?.title || "Scene Diagnostics";
  }

  function renderChatList(chats) {
    if (!chatList) {
      return;
    }
    const heading = chatList.querySelector("h2") || document.createElement("h2");
    heading.textContent = "Chats";
    chatList.replaceChildren(heading);
    for (const chat of chats || []) {
      const row = document.createElement("button");
      row.className = "chat-row";
      row.classList.toggle("active", chat.id === activeChatId);
      row.type = "button";
      row.dataset.chatId = chat.id;
      row.innerHTML = [
        '<svg class="svg-icon" viewBox="0 0 24 24" aria-hidden="true">',
        '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8Z"></path>',
        "</svg>",
        "<span></span>",
        "<time></time>",
      ].join("");
      row.querySelector("span").textContent = chat.title || "New chat";
      row.querySelector("time").textContent = formatChatTime(chat.updated_at_ms);
      row.addEventListener("click", () => {
        send({
          type: "open_chat",
          request_id: nextRequestId("open-chat"),
          chat_id: chat.id,
        });
        appShell?.classList.remove("drawer-open");
      });
      chatList.append(row);
    }
  }

  function formatChatTime(timestampMs) {
    const deltaMs = Date.now() - Number(timestampMs || 0);
    if (!Number.isFinite(deltaMs) || deltaMs < 0) {
      return "now";
    }
    const minutes = Math.floor(deltaMs / 60000);
    if (minutes < 1) {
      return "now";
    }
    if (minutes < 60) {
      return minutes + "m";
    }
    const hours = Math.floor(minutes / 60);
    if (hours < 24) {
      return hours + "h";
    }
    return Math.floor(hours / 24) + "d";
  }

  function formatCostValue(cost) {
    if (cost > 0 && cost < 0.0001) {
      return "$" + cost.toFixed(6);
    }
    if (cost < 0.01) {
      return "$" + cost.toFixed(4);
    }
    return "$" + cost.toFixed(2);
  }

  function applySettings(settings, options = {}) {
    if (!settings) {
      return;
    }
    applyProviderBaseUrls(settings);
    ollamaBaseUrl = providerBaseUrl("ollama");
    applyProviderRegistry(settings);
    hasOpenRouterKey = providerConnected("openrouter") || Boolean(settings.has_openrouter_key);
    hasOllamaCloudKey = providerConnected("ollama-cloud") || Boolean(settings.has_ollama_cloud_key);
    const savedModel = cleanUiModelId(settings.model || settings.default_model || "");
    currentProvider = providerFromModel(savedModel) || currentProvider;
    currentModel = savedModel || currentModel;
    currentReasoningEffort = cleanReasoningEffort(settings.reasoning_effort);
    currentChatSurface = cleanChatSurface(settings.chat_surface);
    if (!currentProvider && hasOpenRouterKey) {
      currentProvider = "openrouter";
    }
    if (chatSurfaceBrowserInput) {
      chatSurfaceBrowserInput.checked = currentChatSurface === CHAT_SURFACE_BROWSER;
    }
    updateChatSurfaceRestartNotice();
    if (ollamaBaseUrlInput) {
      syncOllamaSetupFields();
    }
    if (modelInput) {
      modelInput.value = currentModel;
    }
    updateProviderUi();
    updateModelUi();
    updateChatSize();
    reasoningEffortControls.forEach((control) => {
      control.value = currentReasoningEffort;
    });
    updateComposerEffort();
    const list = document.querySelector("#model-suggestions");
    if (list && Array.isArray(settings.text_model_suggestions)) {
      list.replaceChildren();
      for (const model of settings.text_model_suggestions) {
        const option = document.createElement("option");
        option.value = model;
        list.append(option);
      }
    }
    requestModelList();
  }

  function applyProviderRegistry(settings) {
    const providers = Array.isArray(settings.providers) && settings.providers.length
      ? settings.providers
      : fallbackProviderRegistry(settings);
    providerRegistry = providers.map(normalizeProvider).filter((provider) => provider.id);
    providerMetadata = new Map(providerRegistry.map((provider) => [provider.id, provider]));
    renderProviderOptions();
    modelPicker?.applyCatalog({
      ...(lastModelCatalog || {}),
      providers: providerRegistry,
    });
  }

  function normalizeProvider(provider) {
    const id = String(provider?.id || "").trim();
    const setup = provider?.setup || null;
    if (id && setup?.base_url) {
      providerBaseUrls.set(id, String(setup.base_url));
    }
    return {
      id,
      name: String(provider?.name || id || "Provider"),
      kind: String(provider?.kind || "cloud"),
      auth: provider?.auth || { type: "none" },
      connected: Boolean(provider?.connected),
      model_prefix: String(provider?.model_prefix || (id ? `${id}/` : "")),
      setup,
    };
  }

  function fallbackProviderRegistry(settings) {
    return [
      {
        id: "openrouter",
        name: "OpenRouter",
        kind: "cloud",
        auth: { type: "api_key", env: "OPENROUTER_API_KEY" },
        connected: Boolean(settings.has_openrouter_key),
        model_prefix: "openrouter/",
      },
      {
        id: "ollama",
        name: "Ollama (local)",
        kind: "local",
        auth: { type: "none" },
        connected: true,
        model_prefix: "ollama/",
        setup: {
          type: "base_url",
          default_base_url: DEFAULT_OLLAMA_BASE_URL,
          base_url: settings.ollama_base_url || DEFAULT_OLLAMA_BASE_URL,
        },
      },
      {
        id: "ollama-cloud",
        name: "Ollama Cloud",
        kind: "cloud",
        auth: { type: "api_key", env: "OLLAMA_API_KEY" },
        connected: Boolean(settings.has_ollama_cloud_key),
        model_prefix: "ollama-cloud/",
      },
      {
        id: "deepseek",
        name: "DeepSeek",
        kind: "cloud",
        auth: { type: "api_key", env: "DEEPSEEK_API_KEY" },
        connected: false,
        model_prefix: "deepseek/",
      },
      {
        id: "lmstudio",
        name: "LM Studio",
        kind: "local",
        auth: { type: "none" },
        connected: true,
        model_prefix: "lmstudio/",
        setup: {
          type: "base_url",
          default_base_url: DEFAULT_LOCAL_BASE_URLS.lmstudio,
          base_url: providerBaseUrl("lmstudio"),
        },
      },
    ];
  }

  function applyProviderBaseUrls(settings) {
    providerBaseUrls = new Map(Object.entries(DEFAULT_LOCAL_BASE_URLS));
    const baseUrls = settings?.provider_base_urls || {};
    Object.entries(baseUrls).forEach(([provider, baseUrl]) => {
      const clean = String(baseUrl || "").trim().replace(/\/+$/, "");
      if (provider && clean) {
        providerBaseUrls.set(provider, clean);
      }
    });
    if (settings?.ollama_base_url) {
      providerBaseUrls.set("ollama", String(settings.ollama_base_url).trim().replace(/\/+$/, ""));
    }
  }

  function providerBaseUrl(providerId) {
    const id = String(providerId || "");
    return providerBaseUrls.get(id) || DEFAULT_LOCAL_BASE_URLS[id] || "";
  }

  function providerBaseUrlPayload() {
    return Object.fromEntries(providerBaseUrls.entries());
  }

  function currentModelLabel() {
    return currentModel ? modelPicker?.displayName(currentModel) || currentModel : "No model";
  }

  function providerFromModel(modelId) {
    const clean = cleanUiModelId(modelId);
    const provider = providerRegistry
      .slice()
      .sort((a, b) => b.model_prefix.length - a.model_prefix.length)
      .find((candidate) => candidate.model_prefix && clean.startsWith(candidate.model_prefix));
    if (provider) {
      return provider.id;
    }
    if (clean.includes("/")) {
      return "openrouter";
    }
    return "";
  }

  function providerLabel(provider = currentProvider) {
    const label = providerMetadata.get(provider)?.name;
    if (label) {
      return label;
    }
    return "Choose provider";
  }

  function updateProviderUi() {
    providerStatuses.forEach((status) => {
      status.textContent = providerLabel();
      status.title = providerLabel();
    });
    if (providerDot) {
      providerDot.classList.toggle("is-idle", !currentProvider);
      providerDot.classList.toggle("is-ready", hasUsableModel());
    }
    renderProviderOptions();
  }

  function ollamaProviderLabel() {
    return localProviderLabel("ollama", ollamaStatus);
  }

  function localProviderLabel(providerId, fallbackState = "unknown") {
    const state = localProviderStatuses.get(providerId)?.state || fallbackState;
    if (state === "checking") {
      return "Checking";
    }
    if (state === "ready") {
      return "Connected";
    }
    if (state === "empty") {
      return "No models";
    }
    if (state === "offline") {
      return "Offline";
    }
    return "Not connected";
  }

  function providerStatusLabel(provider) {
    if (provider.kind === "local") {
      return localProviderLabel(provider.id, provider.id === "ollama" ? ollamaStatus : "unknown");
    }
    if (provider.auth?.type === "api_key") {
      return provider.connected ? "Connected" : "Not connected";
    }
    return provider.connected ? "Connected" : "Available";
  }

  function providerRequiresApiKey(providerId) {
    return providerMetadata.get(providerId)?.auth?.type === "api_key";
  }

  function providerConnected(providerId) {
    return Boolean(providerMetadata.get(providerId)?.connected);
  }

  function hasConnectedApiKeyProvider() {
    return providerRegistry.some((provider) => provider.auth?.type === "api_key" && provider.connected);
  }

  function providerUsesBaseUrlSetup(providerId) {
    return providerMetadata.get(providerId)?.setup?.type === "base_url";
  }

  function applyModelCatalog(catalog) {
    lastModelCatalog = catalog || { models: [] };
    openrouterCatalogStatus = lastModelCatalog.catalog_status || openrouterCatalogStatus;
    ollamaStatus = String(lastModelCatalog.ollama_status?.state || ollamaStatus || "unknown");
    ollamaBaseUrl = lastModelCatalog.ollama_status?.base_url || ollamaBaseUrl;
    providerBaseUrls.set("ollama", ollamaBaseUrl);
    localProviderStatuses = new Map(
      Object.entries(lastModelCatalog.local_provider_statuses || {}),
    );
    localProviderStatuses.forEach((status, providerId) => {
      if (status?.base_url) {
        providerBaseUrls.set(providerId, String(status.base_url));
      }
    });
    const models = Array.isArray(lastModelCatalog.models) ? lastModelCatalog.models : [];
    const daemonOllamaModels = models.filter((model) => String(model?.id || "").startsWith("ollama/"));
    ollamaModels = daemonOllamaModels;
    if (currentProviderIsLocal() && !localModelAvailable(currentModel)) {
      currentModel = "";
    }
    modelPicker?.applyCatalog({
      ...lastModelCatalog,
      catalog_status: openrouterCatalogStatus,
      refreshing: catalogRefreshInFlight,
      providers: providerRegistry,
    });
    updateProviderUi();
    updateModelUi();
    updateChatSize();
  }

  function updateModelUi() {
    modelStatuses.forEach((status) => {
      status.textContent = currentModelLabel();
      status.title = currentModel || "No model selected";
    });
    if (modelInput) {
      modelInput.value = currentModel;
    }
    sendButton?.classList.toggle("is-blocked", !hasUsableModel());
  }

  function hasUsableModel() {
    if (!currentModel || !currentProvider) {
      return false;
    }
    if (currentProviderIsLocal()) {
      return localModelAvailable(currentModel);
    }
    if (providerRequiresApiKey(currentProvider)) {
      return providerConnected(currentProvider);
    }
    return true;
  }

  function currentProviderIsLocal() {
    return providerMetadata.get(currentProvider)?.kind === "local";
  }

  function localModelAvailable(modelId) {
    const clean = cleanUiModelId(modelId);
    if (!clean) {
      return false;
    }
    return (lastModelCatalog?.models || []).some((model) => {
      const id = String(model?.id || "");
      return id === clean && model?.source === "local";
    });
  }

  function selectModel(modelId) {
    const clean = window.FennaraModelPicker?.cleanModelId(modelId) || String(modelId || "").trim();
    if (!clean) {
      return;
    }
    currentProvider = providerFromModel(clean) || currentProvider;
    currentModel = clean;
    updateProviderUi();
    updateModelUi();
    updateChatSize();
    saveCurrentChatSettings();
  }

  function cleanReasoningEffort(effort) {
    return ["low", "medium", "high"].includes(effort) ? effort : "medium";
  }

  function cleanChatSurface(surface) {
    return surface === CHAT_SURFACE_BROWSER ? CHAT_SURFACE_BROWSER : CHAT_SURFACE_EMBEDDED;
  }

  function selectedChatSurface() {
    return chatSurfaceBrowserInput?.checked ? CHAT_SURFACE_BROWSER : CHAT_SURFACE_EMBEDDED;
  }

  function updateChatSurfaceRestartNotice(surface = selectedChatSurface()) {
    if (chatSurfaceRestartStatus) {
      chatSurfaceRestartStatus.hidden = cleanChatSurface(surface) === RUNTIME_CHAT_SURFACE;
    }
  }

  function effortLabel(effort) {
    return effort.charAt(0).toUpperCase() + effort.slice(1);
  }

  function updateComposerEffort() {
    if (effortStatus) {
      effortStatus.textContent = effortLabel(currentReasoningEffort);
    }
    effortOptionButtons.forEach((button) => {
      const selected = button.value === currentReasoningEffort;
      button.setAttribute("aria-selected", String(selected));
    });
  }

  function setEffortMenuOpen(open) {
    if (!effortOptions || !effortToggle) {
      return;
    }
    effortOptions.hidden = !open;
    effortToggle.setAttribute("aria-expanded", String(open));
  }

  function saveCurrentChatSettings() {
    const payload = {
      type: "save_settings",
      request_id: nextRequestId("silent-settings"),
      model: cleanUiModelId(modelInput?.value || currentModel),
      reasoning_effort: currentReasoningEffort,
      ollama_base_url: ollamaBaseUrl,
      provider_base_urls: providerBaseUrlPayload(),
    };
    return send(payload);
  }

  function chooseProvider(provider) {
    if (!providerMetadata.has(provider)) {
      return;
    }
    currentProvider = provider;
    closeProviderPicker();
    if (currentModel && providerFromModel(currentModel) !== provider) {
      currentModel = "";
    }
    updateProviderUi();
    updateModelUi();
    updateChatSize();
    if (providerRequiresApiKey(provider) && !providerConnected(provider)) {
      openProviderKeyPrompt(provider);
      requestModelList();
      return;
    }
    if (providerUsesBaseUrlSetup(provider)) {
      openOllamaSetupPrompt();
      requestModelList({ refreshOllama: true });
      return;
    }
    requestModelList();
  }

  function saveOllamaProvider() {
    const provider = currentProvider && providerUsesBaseUrlSetup(currentProvider) ? currentProvider : "ollama";
    const defaultBaseUrl = providerMetadata.get(provider)?.setup?.default_base_url || DEFAULT_OLLAMA_BASE_URL;
    const nextBaseUrl = String(ollamaBaseUrlInput?.value || providerBaseUrl(provider) || defaultBaseUrl).trim() || defaultBaseUrl;
    providerBaseUrls.set(provider, nextBaseUrl.replace(/\/+$/, ""));
    if (provider === "ollama") {
      ollamaBaseUrl = providerBaseUrl(provider);
    }
    currentProvider = provider;
    closeOllamaSetupPrompt();
    send({
      type: "save_settings",
      request_id: nextRequestId("save-local-provider"),
      reasoning_effort: currentReasoningEffort,
      ollama_base_url: ollamaBaseUrl,
      provider_base_urls: providerBaseUrlPayload(),
    });
    requestModelList({ refreshOllama: true });
    modelPicker?.open();
    appendSystem(`Checking local ${providerLabel(provider)} models.`);
    window.setTimeout(clearSystemStatus, 1200);
  }

  function setSettingsSaving(saving) {
    if (!saveSettingsButton) {
      return;
    }
    saveSettingsButton.disabled = saving;
    saveSettingsButton.textContent = saving ? "Saving..." : "Save locally";
  }

  function flushPendingSettings() {
    if (!pendingSettingsPayload || !socket || socket.readyState !== WebSocket.OPEN) {
      return false;
    }
    socket.send(JSON.stringify(pendingSettingsPayload));
    return true;
  }

  function queueSettingsSave(payload) {
    pendingSettingsPayload = payload;
    setSettingsSaving(true);
    if (flushPendingSettings()) {
      return true;
    }
    appendSystem("Connecting to local daemon...");
    connect();
    return true;
  }

  function handleDaemonMessage(message) {
    if (message.type === "settings" || message.type === "settings_saved") {
      const requestId = String(message.request_id || "");
      const isExplicitSave = requestId.startsWith("save-settings");
      applySettings(message.settings, { preserveTypedKey: !isExplicitSave });
      if (requestId.startsWith("save-settings-key")) {
        currentProvider = keyPromptProvider || currentProvider;
        if (currentModel && providerFromModel(currentModel) !== currentProvider) {
          currentModel = "";
        }
        updateProviderUi();
        updateModelUi();
        updateChatSize();
      }
      if (requestId.startsWith("save-ollama-provider") || requestId.startsWith("save-local-provider")) {
        currentProvider = providerUsesBaseUrlSetup(currentProvider) ? currentProvider : "ollama";
        if (currentModel && providerFromModel(currentModel) !== currentProvider) {
          currentModel = "";
        }
        updateProviderUi();
        updateModelUi();
        updateChatSize();
      }
      if (message.type === "settings_saved") {
        const restartNeeded = currentChatSurface !== RUNTIME_CHAT_SURFACE;
        if (pendingSettingsPayload?.request_id === message.request_id) {
          pendingSettingsPayload = null;
          setSettingsSaving(false);
          if (!restartNeeded) {
            settingsDialog?.close();
          }
          closeOpenRouterKeyPrompt();
        }
        if (!requestId.startsWith("silent-settings")) {
          appendSystem(
            restartNeeded && requestId.startsWith("save-settings")
              ? "Settings saved locally. Restart Godot for the chat display change to take effect."
              : "Settings saved locally.",
          );
          window.setTimeout(clearSystemStatus, restartNeeded ? 7000 : 1200);
        }
        if (!restartNeeded && hasConnectedApiKeyProvider() && requestId.startsWith("save-settings")) {
          refreshModelCatalog(true);
          modelPicker?.open();
        } else {
          requestModelList();
        }
      } else {
        clearSystemStatus();
      }
      return;
    }
    if (message.type === "chat_reset") {
      clearTranscript();
      setStreaming(false);
      return;
    }
    if (message.type === "chat_list") {
      renderChatList(message.chats || []);
      return;
    }
    if (message.type === "model_list") {
      applyModelCatalog(message.catalog);
      return;
    }
    if (message.type === "catalog_refresh_result") {
      catalogRefreshInFlight = false;
      openrouterCatalogStatus = message.status || openrouterCatalogStatus;
      modelPicker?.applyCatalog({
        ...(lastModelCatalog || {}),
        catalog_status: openrouterCatalogStatus,
        refreshing: false,
        error: message.ok ? null : message.error?.message || "Catalog refresh failed.",
        providers: providerRegistry,
      });
      requestModelList();
      if (!message.ok) {
        appendSystem(message.error?.message || "Could not refresh model catalog.");
        window.setTimeout(clearSystemStatus, 1600);
      }
      return;
    }
    if (message.type === "project_status") {
      applyProjectStatus(message);
      return;
    }
    if (message.type === "chat_opened") {
      activeChatId = message.chat?.id || null;
      updateChatTitle(message.chat);
      renderStoredMessages(message.messages || []);
      if (!message.request_id) {
        restorePendingOptimisticUserMessages(activeChatId);
      }
      if (message.reverted && typeof message.restored_message === "string" && prompt) {
        prompt.value = message.restored_message;
        resizePrompt();
        prompt.focus();
      }
      canRevert = Boolean(message.can_revert);
      updateRevertButton();
      sessionCost = Number(message.chat?.total_cost || 0);
      latestPromptTokens = Number(message.chat?.latest_prompt_tokens || latestPromptTokens || 0);
      updateChatSize();
      updateSessionCost();
      return;
    }
    if (message.type === "chat_created") {
      activeChatId = message.chat?.id || activeChatId;
      updateChatTitle(message.chat);
      sessionCost = Number(message.chat?.total_cost || 0);
      latestPromptTokens = Number(message.chat?.latest_prompt_tokens || 0);
      updateChatSize();
      updateSessionCost();
      return;
    }
    if (message.type === "chat_updated") {
      if (message.chat?.id && (!activeChatId || message.chat.id === activeChatId)) {
        activeChatId = message.chat.id;
        updateChatTitle(message.chat);
        const nextSessionCost = Number(message.chat?.total_cost || sessionCost);
        if (chatStreaming && Number.isFinite(nextSessionCost) && nextSessionCost > sessionCost) {
          activeTurnCost += nextSessionCost - sessionCost;
        }
        sessionCost = nextSessionCost;
        latestPromptTokens = Number(message.chat?.latest_prompt_tokens || latestPromptTokens || 0);
        updateChatSize();
        updateSessionCost();
      }
      return;
    }
    if (message.type === "chat_user_message") {
      clearSystemStatus();
      setStreaming(true);
      transcriptRenderer.resetActiveAssistant();
      activeTurnCost = 0;
      activeChatId = message.chat_id || activeChatId;
      const requestKey = String(message.request_id || "");
      const usedOptimisticMessage = hasConnectedOptimisticUserMessage(requestKey);
      if (message.user_message && !usedOptimisticMessage) {
        appendDaemonUserMessage(message.user_message, requestKey);
      }
      optimisticUserMessageRequests.delete(requestKey);
      canRevert = false;
      updateRevertButton();
      return;
    }
    if (message.type === "chat_stream_start") {
      clearSystemStatus();
      setStreaming(true);
      transcriptRenderer.resetActiveAssistant();
      activeTurnCost = 0;
      activeChatId = message.chat_id || activeChatId;
      if (message.user_message) {
        const requestKey = String(message.request_id || "");
        if (!hasConnectedOptimisticUserMessage(requestKey)) {
          appendDaemonUserMessage(message.user_message, requestKey);
        }
      }
      canRevert = Boolean(message.can_revert);
      updateRevertButton();
      return;
    }
    if (message.type === "chat_item_update" && message.item?.type === "message") {
      updateAssistantText(message.item.content || "");
      return;
    }
    if (message.type === "chat_item_update" && message.item?.type === "reasoning") {
      updateThinkingText(message.item.content || "", message.item.status);
      return;
    }
    if (
      message.type === "chat_item_update" &&
      (message.item?.type === "function_call" || message.item?.type === "tool_result")
    ) {
      updateToolCall(message.item);
      if (message.item?.type === "tool_result") {
        transcriptRenderer.resetActiveAssistant();
      }
      return;
    }
    if (message.type === "chat_response") {
      clearSystemStatus();
      updateAssistantText(message.response || "");
      const cost = usageCost(message.usage);
      if (Number.isFinite(cost)) {
        activeTurnCost += cost;
        sessionCost += cost;
        updateSessionCost();
      }
      latestPromptTokens = usagePromptTokens(message.usage) || latestPromptTokens;
      updateChatSize();
      const turnUsage = { ...(message.usage || {}), cost: activeTurnCost };
      transcriptRenderer.addAssistantActions(turnUsage, formatUsageCost);
      activeTurnCost = 0;
      transcriptRenderer.finishActiveThinking();
      transcriptRenderer.resetActiveAssistant();
      setStreaming(false);
      updateRevertButton();
      optimisticUserMessageRequests.delete(String(message.request_id || ""));
      return;
    }
    if (message.type === "chat_cancelled") {
      clearSystemStatus();
      updateAssistantText(message.response || "");
      transcriptRenderer.finishActiveThinking();
      transcriptRenderer.resetActiveAssistant();
      setStreaming(false);
      canRevert = Boolean(message.can_revert ?? true);
      updateRevertButton();
      appendSystem("Cancelled.");
      window.setTimeout(clearSystemStatus, 1200);
      optimisticUserMessageRequests.delete(String(message.request_id || ""));
      return;
    }
    if (message.type === "error") {
      const errorText = message.message || "Chat request failed.";
      if (chatStreaming) {
        updateAssistantText(`Request failed: ${errorText}`);
      }
      appendSystem(errorText);
      if (pendingSettingsPayload?.request_id === message.request_id) {
        pendingSettingsPayload = null;
        setSettingsSaving(false);
      }
      transcriptRenderer.finishActiveThinking();
      transcriptRenderer.resetActiveAssistant();
      setStreaming(false);
      updateRevertButton();
      optimisticUserMessageRequests.delete(String(message.request_id || ""));
      if (message.code === "provider_auth_error" || message.code === "missing_openrouter_key") {
        if (currentProvider && providerRequiresApiKey(currentProvider)) {
          openProviderKeyPrompt(currentProvider);
        } else {
          openProviderPicker();
        }
      }
    }
  }

  function updateRevertButton() {
    if (!revertButton) {
      return;
    }
    revertButton.disabled = chatStreaming || !canRevert || !activeChatId;
  }

  function toggleDrawer() {
    appShell?.classList.toggle("drawer-open");
  }

  function closeDrawer() {
    appShell?.classList.remove("drawer-open");
  }

  function closeDrawerFromOutsideClick(event) {
    if (!appShell?.classList.contains("drawer-open")) {
      return;
    }
    if (event.target.closest("[data-chat-drawer]") || event.target.closest("[data-toggle-drawer]")) {
      return;
    }
    closeDrawer();
  }

  function startNewChat() {
    closeDrawer();
    clearTranscript(true);
    send({
      type: "new_chat",
      request_id: nextRequestId("new-chat"),
      model: cleanUiModelId(modelInput?.value || currentModel),
      reasoning_effort: currentReasoningEffort,
    });
    prompt.value = "";
    clearAttachments();
    resizePrompt();
    prompt.focus();
  }

  async function addImageFiles(files) {
    const unique = uniqueFiles(files);
    const imageFiles = unique.filter((file) => file && imageMimeType(file));
    if (imageFiles.length === 0) {
      return 0;
    }
    let added = 0;
    for (const file of imageFiles) {
      if (attachedImages.length >= MAX_IMAGE_ATTACHMENTS) {
        appendSystem(`Attach up to ${MAX_IMAGE_ATTACHMENTS} images.`);
        break;
      }
      const mimeType = imageMimeType(file);
      const validationError = validateImageFile(file, mimeType);
      if (validationError) {
        appendSystem(validationError);
        continue;
      }
      try {
        const dataUrl = await readFileAsDataUrl(file);
        const prepared = await prepareImageForChat({
          base64: dataUrl.split(",", 2)[1] || "",
          mimeType,
          name: file.name || "pasted image",
          size: file.size,
        });
        if (!prepared) {
          appendSystem("Image is too large. Try a smaller screenshot.");
          continue;
        }
        const totalSize = attachedImages.reduce((sum, image) => sum + image.size, 0) + prepared.size;
        if (totalSize > MAX_TOTAL_IMAGE_BYTES) {
          appendSystem(`Attached images must be ${formatBytes(MAX_TOTAL_IMAGE_BYTES)} total or less.`);
          continue;
        }
        attachedImages.push({
          id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
          base64: prepared.base64,
          mime_type: prepared.mimeType,
          name: prepared.name,
          size: prepared.size,
          description: file.name || "user image",
        });
        added += 1;
      } catch {
        appendSystem("Could not read that image.");
      }
    }
    renderAttachmentPreview();
    return added;
  }

  function uniqueFiles(files) {
    const seen = new Set();
    const unique = [];
    for (const file of Array.from(files || [])) {
      if (!file) {
        continue;
      }
      const key = [file.name || "", file.type || "", file.size || 0, file.lastModified || 0].join(":");
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      unique.push(file);
    }
    return unique;
  }

  async function addImagePayload(image) {
    const base64 = String(image?.base64 || "");
    const mimeType = String(image?.mime_type || "").toLowerCase();
    const size = Number(image?.size || 0);
    if (!base64 || !mimeType) {
      return false;
    }
    if (attachedImages.length >= MAX_IMAGE_ATTACHMENTS) {
      appendSystem(`Attach up to ${MAX_IMAGE_ATTACHMENTS} images.`);
      return false;
    }
    if (!SUPPORTED_IMAGE_TYPES.has(mimeType)) {
      appendSystem("Unsupported image type. Use PNG, JPEG, WebP, or GIF.");
      return false;
    }
    if (size > MAX_RAW_IMAGE_BYTES) {
      appendSystem("Image is too large. Try a smaller screenshot.");
      return false;
    }
    const prepared = await prepareImageForChat({
      base64,
      mimeType,
      name: String(image?.name || "pasted image"),
      size,
    });
    if (!prepared) {
      appendSystem("Image is too large. Try a smaller screenshot.");
      return false;
    }
    const totalSize = attachedImages.reduce((sum, item) => sum + item.size, 0) + prepared.size;
    if (totalSize > MAX_TOTAL_IMAGE_BYTES) {
      appendSystem(`Attached images must be ${formatBytes(MAX_TOTAL_IMAGE_BYTES)} total or less.`);
      return false;
    }
    attachedImages.push({
      id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
      base64: prepared.base64,
      mime_type: prepared.mimeType,
      name: prepared.name,
      size: prepared.size,
      description: prepared.name,
    });
    renderAttachmentPreview();
    return true;
  }

  function imageMimeType(file) {
    const explicitType = String(file?.type || "").toLowerCase();
    if (SUPPORTED_IMAGE_TYPES.has(explicitType)) {
      return explicitType;
    }
    const name = String(file?.name || "").toLowerCase();
    if (name.endsWith(".png")) {
      return "image/png";
    }
    if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
      return "image/jpeg";
    }
    if (name.endsWith(".webp")) {
      return "image/webp";
    }
    if (name.endsWith(".gif")) {
      return "image/gif";
    }
    return "";
  }

  function validateImageFile(file, mimeType) {
    if (!SUPPORTED_IMAGE_TYPES.has(mimeType)) {
      return "Unsupported image type. Use PNG, JPEG, WebP, or GIF.";
    }
    if (file.size > MAX_RAW_IMAGE_BYTES) {
      return "Image is too large. Try a smaller screenshot.";
    }
    return "";
  }

  async function prepareImageForChat(image) {
    if (!image.base64) {
      return null;
    }
    if (image.size <= MAX_SEND_IMAGE_BYTES) {
      return image;
    }
    if (image.mimeType === "image/gif") {
      return null;
    }
    return compressImageForChat(image);
  }

  async function compressImageForChat(image) {
    const dataUrl = `data:${image.mimeType};base64,${image.base64}`;
    const loaded = await loadImage(dataUrl);
    const canvas = document.createElement("canvas");
    const context = canvas.getContext("2d");
    if (!context) {
      return null;
    }

    let scale = Math.min(1, Math.sqrt(MAX_SEND_IMAGE_BYTES / Math.max(image.size, 1)) * 0.92);
    const qualities = [0.82, 0.72, 0.62, 0.52];
    for (let attempt = 0; attempt < 6; attempt += 1) {
      canvas.width = Math.max(1, Math.round(loaded.width * scale));
      canvas.height = Math.max(1, Math.round(loaded.height * scale));
      context.fillStyle = "#fff";
      context.fillRect(0, 0, canvas.width, canvas.height);
      context.drawImage(loaded, 0, 0, canvas.width, canvas.height);
      for (const quality of qualities) {
        const blob = await canvasToBlob(canvas, "image/jpeg", quality);
        if (blob && blob.size <= MAX_SEND_IMAGE_BYTES) {
          return {
            base64: await blobToBase64(blob),
            mimeType: "image/jpeg",
            name: image.name.replace(/\.[^.]+$/, "") + ".jpg",
            size: blob.size,
          };
        }
      }
      scale *= 0.82;
    }
    return null;
  }

  function loadImage(src) {
    return new Promise((resolve, reject) => {
      const image = new Image();
      image.onload = () => resolve(image);
      image.onerror = reject;
      image.src = src;
    });
  }

  function canvasToBlob(canvas, type, quality) {
    return new Promise((resolve) => {
      canvas.toBlob(resolve, type, quality);
    });
  }

  async function blobToBase64(blob) {
    const dataUrl = await readFileAsDataUrl(blob);
    return dataUrl.split(",", 2)[1] || "";
  }

  function readFileAsDataUrl(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.addEventListener("load", () => resolve(String(reader.result || "")));
      reader.addEventListener("error", reject);
      reader.readAsDataURL(file);
    });
  }

  function renderAttachmentPreview() {
    if (!attachmentPreview) {
      return;
    }
    attachmentPreview.hidden = attachedImages.length === 0;
    attachmentPreview.replaceChildren();
    for (const image of attachedImages) {
      const chip = document.createElement("figure");
      chip.className = "attachment-chip";
      const preview = document.createElement("button");
      preview.type = "button";
      preview.className = "attachment-preview-button";
      preview.setAttribute("aria-label", `Open ${image.name || "attached image"}`);
      const img = document.createElement("img");
      img.alt = image.name || "Attached image";
      img.src = `data:${image.mime_type};base64,${image.base64}`;
      preview.addEventListener("click", () => transcriptRenderer.openImagePreview(img.src, img.alt));
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "attachment-remove-button";
      remove.setAttribute("aria-label", "Remove image");
      remove.textContent = "x";
      remove.addEventListener("click", () => {
        attachedImages = attachedImages.filter((item) => item.id !== image.id);
        renderAttachmentPreview();
      });
      preview.append(img);
      chip.append(preview, remove);
      attachmentPreview.append(chip);
    }
  }

  function clearAttachments() {
    attachedImages = [];
    if (imageInput) {
      imageInput.value = "";
    }
    renderAttachmentPreview();
  }

  function attachmentPayload() {
    return attachedImages.map((image) => ({
      base64: image.base64,
      mime_type: image.mime_type,
      description: image.description,
      name: image.name,
      size: image.size,
    }));
  }

  function formatBytes(bytes) {
    return `${Math.round(bytes / 1024 / 1024)} MB`;
  }

  function nativePasteboardBridge() {
    return window.webkit?.messageHandlers?.fennaraPasteboard;
  }

  function requestNativePastedImage() {
    const bridge = nativePasteboardBridge();
    if (!bridge) {
      return false;
    }
    try {
      bridge.postMessage({ type: "paste_image" });
      return true;
    } catch {
      return false;
    }
  }

  window.FennaraNativePasteboard = {
    receiveImage(image) {
      addImagePayload(image).finally(() => {
        window.setTimeout(resizePrompt, 0);
      });
    },
    receiveError(error) {
      const message = String(error?.message || "Could not paste that image.");
      appendSystem(message);
      window.setTimeout(resizePrompt, 0);
    },
  };

  document.querySelectorAll("[data-open-settings]").forEach((button) => {
    button.addEventListener("click", openSettings);
  });
  document.querySelectorAll("[data-open-model-picker]").forEach((button) => {
    button.addEventListener("click", openModelPicker);
  });
  commandOptionButtons.forEach((button) => {
    button.addEventListener("click", (event) => {
      event.stopPropagation();
      runCommand(button.dataset.commandOption || "");
    });
  });
  providerSearch?.addEventListener("input", renderProviderOptions);
  providerSearch?.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopPropagation();
      closeProviderPicker();
      focusComposer();
    }
  });
  providerKeyPopover?.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopPropagation();
      closeOpenRouterKeyPrompt();
      focusComposer();
    }
  });
  ollamaSetupPopover?.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopPropagation();
      closeOllamaSetupPrompt();
      focusComposer();
    }
  });
  providerKeyForm?.addEventListener("submit", (event) => {
    event.preventDefault();
    const key = providerKeyInlineInput?.value.trim() || "";
    if (!key) {
      providerKeyInlineInput?.focus();
      return;
    }
    const provider = keyPromptProvider || currentProvider || "openrouter";
    currentProvider = provider;
    if (currentModel && providerFromModel(currentModel) !== provider) {
      currentModel = "";
    }
    updateProviderUi();
    updateModelUi();
    updateChatSize();
    const payload = {
      type: "save_settings",
      request_id: nextRequestId("save-settings-key"),
      model: cleanUiModelId(modelInput?.value || currentModel),
      reasoning_effort: currentReasoningEffort,
      ollama_base_url: ollamaBaseUrl,
      provider_base_urls: providerBaseUrlPayload(),
      provider_api_keys: {
        [provider]: key,
      },
    };
    queueSettingsSave(payload);
  });
  ollamaForm?.addEventListener("submit", (event) => {
    event.preventDefault();
    saveOllamaProvider();
  });

  if (reloadButton) {
    reloadButton.hidden = !SHOW_RELOAD_BUTTON;
    if (SHOW_RELOAD_BUTTON) {
      reloadButton.addEventListener("click", reloadUi);
    }
  }
  document.querySelectorAll("[data-copy-code]").forEach((button) => {
    button.addEventListener("click", async () => {
      const code = button.closest(".code-block")?.querySelector("code")?.textContent ?? "";
      if (!code) {
        return;
      }
      await navigator.clipboard?.writeText(code);
      flashCopied(button, "Copy code", "Copied code");
    });
  });
  document.querySelectorAll("[data-toggle-drawer]").forEach((button) => {
    button.addEventListener("click", toggleDrawer);
  });
  document.querySelectorAll("[data-new-chat]").forEach((button) => {
    button.addEventListener("click", startNewChat);
  });
  attachImageButton?.addEventListener("click", () => {
    imageInput?.click();
  });
  imageInput?.addEventListener("change", () => {
    addImageFiles(imageInput.files).finally(() => {
      imageInput.value = "";
    });
  });
  usageContainer?.addEventListener("mouseenter", showUsagePopover);
  usageContainer?.addEventListener("mouseleave", hideUsagePopoverSoon);
  usagePopover?.addEventListener("mouseenter", showUsagePopover);
  usagePopover?.addEventListener("mouseleave", hideUsagePopoverSoon);
  sessionCostStatus?.addEventListener("focus", showUsagePopover);
  sessionCostStatus?.addEventListener("blur", hideUsagePopoverSoon);
  revertButton?.addEventListener("click", () => {
    if (chatStreaming || !activeChatId) {
      return;
    }
    send({
      type: "revert_chat",
      request_id: nextRequestId("revert-chat"),
      chat_id: activeChatId,
    });
  });
  setMcpTargetButton?.addEventListener("click", () => {
    if (setMcpTargetButton.classList.contains("is-target")) {
      return;
    }
    setMcpTargetButton.classList.add("is-setting");
    if (targetPillText) {
      targetPillText.textContent = "Setting";
    }
    send({ type: "set_mcp_target", request_id: nextRequestId("set-target") });
  });
  sendButton?.addEventListener("click", (event) => {
    if (!chatStreaming) {
      return;
    }
    event.preventDefault();
    requestCancel();
  });

  function requestCancel() {
    if (!activeChatId) {
      return;
    }
    appendSystem("Cancelling...");
    const cancelSocket = new WebSocket(chatWsUrl());
    cancelSocket.addEventListener("open", () => {
      cancelSocket.send(JSON.stringify({
        type: "cancel_chat",
        request_id: nextRequestId("cancel-chat"),
        chat_id: activeChatId,
      }));
      window.setTimeout(() => cancelSocket.close(), 120);
    });
    cancelSocket.addEventListener("error", () => {
      appendSystem("Cancel request failed.");
    });
  }

  saveSettingsButton?.addEventListener("click", (event) => {
    event.preventDefault();
    const payload = {
      type: "save_settings",
      request_id: nextRequestId("save-settings"),
      model: cleanUiModelId(modelInput?.value || currentModel),
      reasoning_effort: currentReasoningEffort,
      ollama_base_url: ollamaBaseUrl,
      provider_base_urls: providerBaseUrlPayload(),
      chat_surface: selectedChatSurface(),
    };
    queueSettingsSave(payload);
  });

  chatSurfaceBrowserInput?.addEventListener("change", () => {
    updateChatSurfaceRestartNotice();
  });

  reasoningEffortControls.forEach((control) => {
    control.addEventListener("change", () => {
      currentReasoningEffort = cleanReasoningEffort(control.value);
      reasoningEffortControls.forEach((nextControl) => {
        nextControl.value = currentReasoningEffort;
      });
      updateComposerEffort();
      saveCurrentChatSettings();
    });
  });
  effortToggle?.addEventListener("click", (event) => {
    event.stopPropagation();
    setEffortMenuOpen(effortOptions?.hidden !== false);
  });
  effortOptionButtons.forEach((button) => {
    button.addEventListener("click", (event) => {
      event.stopPropagation();
      currentReasoningEffort = cleanReasoningEffort(button.value);
      reasoningEffortControls.forEach((control) => {
        control.value = currentReasoningEffort;
      });
      updateComposerEffort();
      setEffortMenuOpen(false);
      saveCurrentChatSettings();
    });
  });
  document.addEventListener("click", (event) => {
    closeDrawerFromOutsideClick(event);
    if (
      commandPopover &&
      commandPopover.hidden === false &&
      !commandPopover.contains(event.target) &&
      !prompt?.contains(event.target)
    ) {
      closeCommandPalette();
    }
    if (
      providerPopover &&
      providerPopover.hidden === false &&
      !providerPopover.contains(event.target)
    ) {
      closeProviderPicker();
    }
    if (
      providerKeyPopover &&
      providerKeyPopover.hidden === false &&
      !providerKeyPopover.contains(event.target)
    ) {
      closeOpenRouterKeyPrompt();
    }
    if (
      ollamaSetupPopover &&
      ollamaSetupPopover.hidden === false &&
      !ollamaSetupPopover.contains(event.target)
    ) {
      closeOllamaSetupPrompt();
    }
    setEffortMenuOpen(false);
    setUsagePopoverOpen(false);
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      const hadOverlayOpen =
        providerPopover?.hidden === false ||
        providerKeyPopover?.hidden === false ||
        ollamaSetupPopover?.hidden === false ||
        modelPopover?.hidden === false ||
        commandPopover?.hidden === false ||
        effortOptions?.hidden === false ||
        usagePopover?.hidden === false;
      setEffortMenuOpen(false);
      setUsagePopoverOpen(false);
      modelPicker?.close();
      closeProviderPicker();
      closeOpenRouterKeyPrompt();
      closeOllamaSetupPrompt();
      closeCommandPalette();
      closeDrawer();
      if (hadOverlayOpen) {
        event.preventDefault();
        event.stopPropagation();
        focusComposer();
      }
    }
  }, true);
  window.addEventListener("resize", () => {
    positionUsagePopover();
    positionProviderPopover();
    positionProviderKeyPrompt();
    positionOllamaSetupPrompt();
    positionCommandPalette();
  });
  window.addEventListener("scroll", () => {
    positionUsagePopover();
    positionProviderPopover();
    positionProviderKeyPrompt();
    positionOllamaSetupPrompt();
    positionCommandPalette();
  }, true);

  composer?.addEventListener("submit", (event) => {
    event.preventDefault();
    if (chatStreaming) {
      return;
    }
    const text = prompt.value.trim();
    if (!text && attachedImages.length === 0) {
      return;
    }
    if (!currentProvider) {
      openProviderPicker();
      return;
    }
    if (!currentModel) {
      openModelPicker();
      return;
    }
    if (providerRequiresApiKey(currentProvider) && !providerConnected(currentProvider)) {
      openProviderKeyPrompt(currentProvider);
      return;
    }
    const model = cleanUiModelId(modelInput?.value || currentModel);
    currentReasoningEffort = cleanReasoningEffort(currentReasoningEffort);
    transcriptRenderer.resetStreamState();
    const requestId = nextRequestId("chat");
    const payload = {
      type: "send_chat",
      request_id: requestId,
      chat_id: activeChatId,
      message: text,
      model,
      reasoning_effort: currentReasoningEffort,
    };
    const images = attachmentPayload();
    if (images.length > 0) {
      payload.images = images;
    }
    if (send(payload)) {
      setStreaming(true);
      activeTurnCost = 0;
      canRevert = false;
      updateRevertButton();
      const optimisticNode = appendMessage("user", text, images);
      optimisticUserMessageRequests.set(requestId, {
        node: optimisticNode,
        text,
        images,
        chatId: activeChatId,
      });
      prompt.value = "";
      clearAttachments();
      resizePrompt();
    } else {
      optimisticUserMessageRequests.delete(requestId);
    }
  });

  function cleanUiModelId(modelId) {
    return window.FennaraModelPicker?.cleanModelId(modelId) || String(modelId || "").trim();
  }

  function resizePrompt() {
    if (!prompt) {
      return;
    }
    prompt.style.height = "auto";
    const nextHeight = Math.min(prompt.scrollHeight, PROMPT_MAX_HEIGHT);
    prompt.style.height = nextHeight + "px";
    prompt.style.overflowY = prompt.scrollHeight > PROMPT_MAX_HEIGHT ? "auto" : "hidden";
  }

  prompt?.addEventListener("keydown", (event) => {
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "v") {
      window.setTimeout(requestNativePastedImage, 0);
      return;
    }
    if (commandPopover && commandPopover.hidden === false) {
      if (event.key === "ArrowDown") {
        event.preventDefault();
        moveCommandSelection(1);
        return;
      }
      if (event.key === "ArrowUp") {
        event.preventDefault();
        moveCommandSelection(-1);
        return;
      }
      if (event.key === "Escape") {
        event.preventDefault();
        closeCommandPalette();
        return;
      }
      if (event.key === "Enter" && !event.shiftKey && !event.ctrlKey && !event.altKey && !event.metaKey) {
        const button = visibleCommandButtons()[activeCommandIndex];
        if (button) {
          event.preventDefault();
          runCommand(button.dataset.commandOption || "");
          return;
        }
      }
    }
    if (event.key !== "Enter" || event.shiftKey || event.ctrlKey || event.altKey || event.metaKey) {
      return;
    }
    event.preventDefault();
    composer?.requestSubmit();
  });
  prompt?.addEventListener("input", () => {
    resizePrompt();
    updateCommandPalette();
  });
  prompt?.addEventListener("paste", (event) => {
    const directFiles = Array.from(event.clipboardData?.files || []);
    const itemFiles = Array.from(event.clipboardData?.items || [])
      .filter((item) => item.kind === "file")
      .map((item) => item.getAsFile())
      .filter(Boolean);
    const files = [...directFiles, ...itemFiles];
    if (files.length > 0) {
      addImageFiles(files).then((added) => {
        if (added === 0) {
          requestNativePastedImage();
        }
      });
    } else {
      requestNativePastedImage();
    }
    window.setTimeout(resizePrompt, 0);
  });

  clearTranscript();
  appendSystem("Connecting to local daemon...");
  resizePrompt();
  updateProviderUi();
  updateModelUi();
  updateChatSize();
  updateSessionCost();
  connect();
})();
