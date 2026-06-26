(function () {
  function createModelPicker(options) {
    const popover = options.popover;
    const trigger = options.trigger;
    const search = options.search;
    const list = options.list;
    const detail = options.detail;
    const getCurrentModel = options.getCurrentModel;
    const getCurrentProvider = options.getCurrentProvider;
    const getProviders = options.getProviders || (() => []);
    const isProviderConnected = options.isProviderConnected || (() => false);
    const providerFromModel = options.providerFromModel || defaultProviderFromModel;
    const getOllamaModels = options.getOllamaModels;
    const openProviderPicker = options.openProviderPicker;
    const openOpenRouterKeyPrompt = options.openOpenRouterKeyPrompt;
    const openProviderKeyPrompt = options.openProviderKeyPrompt || openOpenRouterKeyPrompt;
    const onSelect = options.onSelect;
    const onEscapeClose = options.onEscapeClose;
    const onRequestModels = options.onRequestModels;
    const onRefreshCatalog = options.onRefreshCatalog;
    const RECENT_MODEL_STORAGE_KEY = "fennara.chat.recentModels";
    const COLLAPSED_SECTION_STORAGE_KEY = "fennara.chat.collapsedModelSections";

    let catalog = [];
    let catalogStatus = null;
    let catalogError = "";
    let refreshing = false;
    let activeIndex = -1;
    let visibleModels = [];
    let searchFocusTimer = 0;
    let recentModelIds = loadRecentModelIds();
    let collapsedSectionKeys = loadCollapsedSectionKeys();

    function open() {
      if (!popover || !trigger) {
        return false;
      }
      popover.hidden = false;
      trigger.setAttribute("aria-expanded", "true");
      requestModels();
      render();
      positionPopover();
      window.clearTimeout(searchFocusTimer);
      searchFocusTimer = window.setTimeout(() => {
        if (popover.hidden === false) {
          search?.focus({ preventScroll: true });
        }
      }, 0);
      return true;
    }

    function close(reason = "") {
      if (!popover || popover.hidden) {
        return;
      }
      window.clearTimeout(searchFocusTimer);
      popover.hidden = true;
      trigger?.setAttribute("aria-expanded", "false");
      activeIndex = -1;
      renderDetail(null);
      if (reason === "escape") {
        onEscapeClose?.();
      }
    }

    function toggle() {
      return popover?.hidden === false ? (close(), true) : open();
    }

    function applyCatalog(nextCatalog) {
      catalog = Array.isArray(nextCatalog?.models) ? nextCatalog.models : [];
      catalogStatus = nextCatalog?.catalog_status || catalogStatus;
      catalogError = String(nextCatalog?.error || "");
      refreshing = Boolean(nextCatalog?.refreshing);
      render();
      if (popover && popover.hidden === false) {
        positionPopover();
      }
    }

    function displayName(modelId) {
      const model = allModels().find((entry) => entry.id === modelId);
      return model?.display_name || fallbackModelName(modelId);
    }

    function modelInfo(modelId) {
      return allModels().find((entry) => entry.id === modelId) || null;
    }

    function requestModels() {
      onRequestModels?.();
    }

    function select(modelId) {
      const clean = cleanModelId(modelId);
      if (!clean) {
        return;
      }
      rememberRecentModel(clean);
      onSelect?.(clean);
      close();
    }

    function allModels() {
      const models = [];
      const seen = new Set();
      for (const model of catalog) {
        const providerId = modelProviderId(model);
        const provider = providerMetadata(providerId);
        if (provider?.auth?.type === "api_key" && !isProviderConnected(providerId)) {
          continue;
        }
        if (provider?.kind === "local" && !modelAvailableFromDaemon(model)) {
          continue;
        }
        if (!seen.has(model.id)) {
          seen.add(model.id);
          models.push(model);
        }
      }
      for (const model of getOllamaModels?.() || []) {
        if (!seen.has(model.id)) {
          seen.add(model.id);
          models.push(model);
        }
      }
      return models;
    }

    function render() {
      if (!list) {
        return;
      }
      const provider = getCurrentProvider?.() || "";
      const query = (search?.value || "").trim().toLowerCase();
      const models = allModels();
      const rankedModels = models
        .map((model, index) => ({ model, index, score: modelSearchScore(model, query) }))
        .filter((entry) => entry.score < 99)
        .sort((a, b) => a.score - b.score || a.index - b.index)
        .map((entry) => entry.model);
      list.replaceChildren();
      renderDetail(null);

      if (!provider && !rankedModels.length) {
        renderEmpty("Add a model provider before /model.", {
          label: "Open provider picker",
          onClick: openProviderPicker,
        });
        return;
      }
      if (provider && providerRequiresApiKey(provider) && !isProviderConnected(provider) && !rankedModels.length) {
        renderEmpty(`${providerLabel(provider)} not connected.`, {
          label: "Add API key",
          onClick: () => openProviderKeyPrompt?.(provider),
        });
        return;
      }
      if (!rankedModels.length) {
        if (hasConnectedApiKeyProvider() && !query) {
          renderEmpty(openrouterEmptyMessage(), {
            label: refreshing ? "" : "Refresh",
            onClick: () => {
              refreshing = true;
              render();
              onRefreshCatalog?.();
            },
          });
          return;
        }
        renderEmpty("No matching models.", null);
        return;
      }

      const currentModel = getCurrentModel?.() || "";
      const sections = modelSections(rankedModels, query, currentModel);
      const visibleSections = sections.map((section) => ({
        ...section,
        collapsed: isSectionCollapsed(section, query),
      }));
      visibleModels = visibleSections.flatMap((section) =>
        section.collapsed ? [] : section.models,
      );
      if (!visibleModels.length) {
        activeIndex = -1;
      } else if (activeIndex < 0 || activeIndex >= visibleModels.length) {
        activeIndex = Math.max(0, visibleModels.findIndex((model) => model.id === currentModel));
      }
      let rowIndex = 0;
      visibleSections.forEach((section) => {
        if (section.label) {
          renderSectionHeading(section, query);
        }
        if (!section.collapsed) {
          section.models.forEach((model) => renderModelRow(model, currentModel, rowIndex++));
        }
      });
    }

    function renderSectionHeading(section, query) {
      const collapsible = isCollapsibleSection(section, query);
      if (!collapsible) {
        const heading = document.createElement("div");
        heading.className = "model-section-label";
        heading.textContent = section.label;
        list.append(heading);
        return;
      }

      const button = document.createElement("button");
      button.type = "button";
      button.className = "model-section-label model-section-toggle";
      button.setAttribute("aria-expanded", String(!section.collapsed));
      button.innerHTML = [
        `<span>${escapeHtml(section.label)}</span>`,
        '<span class="model-section-chevron" aria-hidden="true"></span>',
      ].join("");
      button.addEventListener("click", () => toggleSection(section.key));
      list.append(button);
    }

    function renderModelRow(model, currentModel, index) {
      const row = document.createElement("button");
      row.type = "button";
      row.className = "model-row";
      row.id = "model-option-" + index;
      row.setAttribute("role", "option");
      row.dataset.selected = String(model.id === currentModel);
      row.setAttribute("aria-selected", String(index === activeIndex));
      row.innerHTML = [
        '<span class="model-row-main">',
        `<strong>${escapeHtml(model.display_name || fallbackModelName(model.id))}</strong>`,
        "</span>",
        `<span class="model-row-provider">${escapeHtml(modelProviderLabel(model))}</span>`,
      ].join("");
      row.addEventListener("mouseenter", () => setActive(index));
      row.addEventListener("focus", () => setActive(index));
      row.addEventListener("click", () => select(model.id));
      list.append(row);
    }

    function modelSections(models, query, currentModel) {
      const sections = [];
      const recent = query ? [] : recentModels(models, currentModel);
      const providerSections = new Map();
      if (recent.length) {
        sections.push({ key: "recent", label: "Recent", models: recent });
      }
      models.forEach((model) => {
        const label = modelGroupLabel(model);
        if (!providerSections.has(label)) {
          providerSections.set(label, []);
        }
        providerSections.get(label).push(model);
      });
      providerSections.forEach((sectionModels, label) => {
        sections.push({ key: "provider:" + label.toLowerCase(), label, models: sectionModels });
      });
      return sections;
    }

    function isCollapsibleSection(section, query) {
      return !query && section.key !== "recent" && section.models.length > 0;
    }

    function isSectionCollapsed(section, query) {
      return isCollapsibleSection(section, query) && collapsedSectionKeys.has(section.key);
    }

    function toggleSection(key) {
      if (!key) {
        return;
      }
      if (collapsedSectionKeys.has(key)) {
        collapsedSectionKeys.delete(key);
      } else {
        collapsedSectionKeys.add(key);
      }
      saveCollapsedSectionKeys();
      render();
      positionPopover();
    }

    function recentModels(models, currentModel) {
      const ids = [currentModel, ...recentModelIds].filter(Boolean);
      const used = new Set();
      const recent = [];
      ids.forEach((id) => {
        if (used.has(id)) {
          return;
        }
        const model = models.find((entry) => entry.id === id);
        if (!model) {
          return;
        }
        used.add(id);
        recent.push(model);
      });
      return recent.slice(0, 4);
    }

    function rememberRecentModel(modelId) {
      recentModelIds = [modelId, ...recentModelIds.filter((id) => id !== modelId)].slice(0, 8);
      try {
        window.localStorage?.setItem(RECENT_MODEL_STORAGE_KEY, JSON.stringify(recentModelIds));
      } catch {
        // Ignore private-mode/localStorage failures.
      }
    }

    function loadRecentModelIds() {
      try {
        const raw = window.localStorage?.getItem(RECENT_MODEL_STORAGE_KEY);
        const parsed = raw ? JSON.parse(raw) : [];
        return Array.isArray(parsed) ? parsed.map(cleanModelId).filter(Boolean).slice(0, 8) : [];
      } catch {
        return [];
      }
    }

    function loadCollapsedSectionKeys() {
      try {
        const raw = window.localStorage?.getItem(COLLAPSED_SECTION_STORAGE_KEY);
        const parsed = raw ? JSON.parse(raw) : [];
        return new Set(Array.isArray(parsed) ? parsed.filter((key) => typeof key === "string") : []);
      } catch {
        return new Set();
      }
    }

    function saveCollapsedSectionKeys() {
      try {
        window.localStorage?.setItem(
          COLLAPSED_SECTION_STORAGE_KEY,
          JSON.stringify(Array.from(collapsedSectionKeys)),
        );
      } catch {
        // Ignore private-mode/localStorage failures.
      }
    }

    function modelProviderLabel(model) {
      return providerLabel(modelProviderId(model)) || model?.provider || "Other";
    }

    function modelGroupLabel(model) {
      return modelProviderLabel(model);
    }

    function modelProviderId(model) {
      const explicit = String(model?.provider_id || "").trim();
      if (explicit) {
        return explicit;
      }
      return providerFromModel(String(model?.id || ""));
    }

    function providerMetadata(providerId) {
      return getProviders().find((provider) => provider.id === providerId) || null;
    }

    function providerLabel(providerId) {
      return providerMetadata(providerId)?.name || "";
    }

    function providerRequiresApiKey(providerId) {
      return providerMetadata(providerId)?.auth?.type === "api_key";
    }

    function hasConnectedApiKeyProvider() {
      return getProviders().some((provider) => provider.auth?.type === "api_key" && isProviderConnected(provider.id));
    }

    function modelAvailableFromDaemon(model) {
      const id = String(model?.id || "");
      if (!id) {
        return false;
      }
      if (model.source === "local") {
        return true;
      }
      return (getOllamaModels?.() || []).some((entry) => entry.id === id);
    }

    function modelSearchScore(model, query) {
      if (!query) {
        return 0;
      }
      return searchScore([model.display_name, searchableModelId(model)], query);
    }

    function searchScore(values, query) {
      const normalized = values
        .filter(Boolean)
        .map((value) => String(value).toLowerCase());
      if (normalized.some((value) => value === query)) {
        return 0;
      }
      if (normalized.some((value) => value.startsWith(query))) {
        return 1;
      }
      if (normalized.some((value) => value.split(/[\s/:_-]+/).some((part) => part.startsWith(query)))) {
        return 2;
      }
      if (normalized.some((value) => value.includes(query))) {
        return 3;
      }
      return 99;
    }

    function searchableModelId(model) {
      const id = String(model?.id || "");
      const providerId = modelProviderId(model);
      const prefix = providerId ? providerId + "/" : "";
      return prefix && id.startsWith(prefix) ? id.slice(prefix.length) : id;
    }

    function openrouterEmptyMessage() {
      if (refreshing) {
        return "Loading catalog models...";
      }
      if (catalogStatus?.state === "empty" || catalogError) {
        return "No catalog models yet.";
      }
      return "No catalog models.";
    }

    function renderEmpty(message, action) {
      const empty = document.createElement("div");
      empty.className = "model-empty";
      const text = document.createElement("p");
      text.textContent = message;
      empty.append(text);
      if (action?.label) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "secondary-button";
        button.textContent = action.label;
        button.addEventListener("click", (event) => {
          event.stopPropagation();
          action.onClick?.();
        });
        empty.append(button);
      }
      list.append(empty);
      activeIndex = -1;
      renderDetail(null);
    }

    function setActive(index) {
      activeIndex = index;
      markActiveRow();
    }

    function renderDetail() {
      if (!detail) {
        return;
      }
      detail.hidden = true;
      detail.replaceChildren();
    }

    function positionPopover() {
      if (!popover) {
        return;
      }
      const viewportPad = 10;
      const width = Math.min(560, Math.max(300, window.innerWidth - viewportPad * 2));
      const maxHeight = Math.min(470, window.innerHeight - viewportPad * 2);
      popover.style.width = width + "px";
      popover.style.maxHeight = maxHeight + "px";
      popover.style.left = "50%";
      popover.style.top = "50%";
      popover.style.transform = "translate(-50%, -50%)";
      popover.dataset.side = "center";
    }

    function positionDetail(anchor) {
      if (!detail || !anchor || detail.hidden) {
        return;
      }
      const gap = 10;
      const viewportPad = 10;
      const anchorRect = anchor.getBoundingClientRect();
      const width = Math.min(320, Math.max(230, window.innerWidth - viewportPad * 2));
      detail.style.width = width + "px";
      detail.style.left = Math.min(anchorRect.right + gap, window.innerWidth - viewportPad - width) + "px";
      detail.style.top = Math.max(viewportPad, anchorRect.top) + "px";
    }

    function moveActive(delta) {
      if (!visibleModels.length) {
        return;
      }
      activeIndex = (activeIndex + delta + visibleModels.length) % visibleModels.length;
      const row = list?.querySelectorAll(".model-row")?.[activeIndex];
      markActiveRow();
    }

    function markActiveRow() {
      const rows = Array.from(list?.querySelectorAll(".model-row") || []);
      rows.forEach((row, index) => {
        const active = index === activeIndex;
        row.setAttribute("aria-selected", String(active));
        if (active) {
          row.scrollIntoView({ block: "nearest" });
        }
      });
    }

    search?.addEventListener("input", render);
    search?.addEventListener("keydown", (event) => {
      if (event.key === "ArrowDown") {
        event.preventDefault();
        moveActive(1);
      } else if (event.key === "ArrowUp") {
        event.preventDefault();
        moveActive(-1);
      } else if (event.key === "Enter" && activeIndex >= 0) {
        event.preventDefault();
        select(visibleModels[activeIndex]?.id);
      } else if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        close("escape");
      }
    });
    trigger?.addEventListener("keydown", (event) => {
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        open();
      } else if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        close("escape");
      }
    });
    document.addEventListener("pointerdown", (event) => {
      if (!popover || popover.hidden) {
        return;
      }
      if (popover.contains(event.target) || trigger?.contains(event.target)) {
        return;
      }
      close();
    });
    window.addEventListener("resize", positionPopover);
    window.addEventListener("scroll", positionPopover, true);
    return { open, close, toggle, applyCatalog, displayName, modelInfo, requestModels };
  }

  function cleanModelId(modelId) {
    return String(modelId || "").trim().replace(/:nitro\s*$/i, "");
  }

  function defaultProviderFromModel(modelId) {
    const clean = cleanModelId(modelId);
    if (clean.startsWith("ollama-cloud/")) {
      return "ollama-cloud";
    }
    if (clean.startsWith("ollama/")) {
      return "ollama";
    }
    if (clean.startsWith("lmstudio/")) {
      return "lmstudio";
    }
    if (clean.startsWith("deepseek/")) {
      return "deepseek";
    }
    if (clean.startsWith("openrouter/") || clean.includes("/")) {
      return "openrouter";
    }
    return "";
  }

  function fallbackModelName(modelId) {
    return String(modelId || "")
      .replace(/^~/, "")
      .split("/")
      .pop()
      .replace(/-/g, " ")
      .replace(/\blatest\b/gi, "Latest") || "No model";
  }

  function formatNumber(value) {
    return Number(value || 0).toLocaleString("en-US");
  }

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  window.FennaraModelPicker = { createModelPicker, cleanModelId };
})();
