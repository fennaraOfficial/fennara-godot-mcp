(function () {
  function createModelPicker(options) {
    const popover = options.popover;
    const trigger = options.trigger;
    const search = options.search;
    const list = options.list;
    const detail = options.detail;
    const customInput = options.customInput;
    const addCustomButton = options.addCustomButton;
    const getCurrentModel = options.getCurrentModel;
    const onSelect = options.onSelect;
    const onRequestModels = options.onRequestModels;

    let catalog = [];
    let live = false;
    let hoveredModel = null;
    let activeIndex = -1;
    let visibleModels = [];
    let requestedModels = false;

    function open() {
      if (!popover || !trigger) {
        return false;
      }
      popover.hidden = false;
      trigger.setAttribute("aria-expanded", "true");
      positionPopover();
      render();
      requestModels();
      window.setTimeout(() => search?.focus(), 0);
      return true;
    }

    function close() {
      if (!popover || popover.hidden) {
        return;
      }
      popover.hidden = true;
      trigger?.setAttribute("aria-expanded", "false");
      trigger?.removeAttribute("aria-activedescendant");
      hoveredModel = null;
      activeIndex = -1;
      renderDetail(null);
    }

    function toggle() {
      return popover?.hidden === false ? (close(), true) : open();
    }

    function applyCatalog(nextCatalog) {
      catalog = Array.isArray(nextCatalog?.models) ? nextCatalog.models : [];
      live = Boolean(nextCatalog?.live);
      render();
      if (popover && popover.hidden === false) {
        positionPopover();
      }
    }

    function displayName(modelId) {
      const model = catalog.find((entry) => entry.id === modelId);
      return model?.display_name || fallbackModelName(modelId);
    }

    function modelInfo(modelId) {
      return catalog.find((entry) => entry.id === modelId) || null;
    }

    function requestModels() {
      if (requestedModels) {
        return;
      }
      requestedModels = true;
      onRequestModels?.();
    }

    function select(modelId) {
      const clean = cleanModelId(modelId);
      if (!clean) {
        return;
      }
      onSelect?.(clean);
      close();
    }

    function render() {
      if (!list) {
        return;
      }
      const query = (search?.value || "").trim().toLowerCase();
      visibleModels = catalog.filter((model) => {
        if (!query) {
          return true;
        }
        return [model.id, model.display_name, model.provider]
          .filter(Boolean)
          .some((value) => String(value).toLowerCase().includes(query));
      });
      list.replaceChildren();
      if (!visibleModels.length) {
        const empty = document.createElement("p");
        empty.className = "model-empty";
        empty.textContent = live ? "No matching models." : "Loading OpenRouter models...";
        list.append(empty);
        activeIndex = -1;
        renderDetail(null);
        return;
      }
      const currentModel = getCurrentModel?.() || "";
      if (activeIndex < 0 || activeIndex >= visibleModels.length) {
        activeIndex = Math.max(0, visibleModels.findIndex((model) => model.id === currentModel));
      }
      visibleModels.forEach((model, index) => {
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
          `<small>${escapeHtml(model.id)}</small>`,
          "</span>",
        ].join("");
        row.addEventListener("mouseenter", () => {
          activeIndex = index;
          hoveredModel = model;
          setActiveDescendant(row.id);
          markActiveRow();
          renderDetail(model, row);
        });
        row.addEventListener("mouseleave", () => {
          if (hoveredModel?.id === model.id && document.activeElement !== row) {
            hoveredModel = null;
            renderDetail(null);
          }
        });
        row.addEventListener("focus", () => {
          activeIndex = index;
          hoveredModel = model;
          setActiveDescendant(row.id);
          markActiveRow();
          renderDetail(model, row);
        });
        row.addEventListener("blur", () => {
          if (hoveredModel?.id === model.id) {
            hoveredModel = null;
            renderDetail(null);
          }
        });
        row.addEventListener("click", () => select(model.id));
        list.append(row);
      });
    }

    function renderDetail(model, anchor) {
      if (!detail) {
        return;
      }
      detail.hidden = !model;
      if (!model) {
        detail.replaceChildren();
        return;
      }
      const context = model.context_length ? formatNumber(model.context_length) : "unknown";
      const maxOutput = model.max_output_tokens ? formatNumber(model.max_output_tokens) : "unknown";
      const input = formatPrice(model.input_cost_per_million);
      const completion = formatPrice(model.output_cost_per_million);
      const rows = [
        ["Context", context],
        ["Max output", maxOutput],
        ["Input", input + "/1M"],
        ["Output", completion + "/1M"],
      ];
      if (model.tokens_per_second) {
        rows.push(["Speed", formatNumber(model.tokens_per_second) + " tok/s"]);
      }
      detail.innerHTML = [
        '<div class="model-detail-title">',
        `<strong>${escapeHtml(model.display_name || fallbackModelName(model.id))}</strong>`,
        `<code>${escapeHtml(model.id)}</code>`,
        "</div>",
        '<div class="model-detail-grid">',
        ...rows.map(([label, value]) => `<span>${escapeHtml(label)} <b>${escapeHtml(value)}</b></span>`),
        "</div>",
      ].join("");
      positionDetail(anchor);
    }

    function positionPopover() {
      if (!popover || !trigger) {
        return;
      }
      const gap = 8;
      const viewportPad = 10;
      const triggerRect = trigger.getBoundingClientRect();
      const width = Math.min(360, Math.max(280, window.innerWidth - viewportPad * 2));
      const left = Math.min(
        Math.max(viewportPad, triggerRect.left),
        window.innerWidth - width - viewportPad,
      );
      const spaceAbove = triggerRect.top - viewportPad - gap;
      const spaceBelow = window.innerHeight - triggerRect.bottom - viewportPad - gap;
      const openUp = spaceAbove >= 260 || spaceAbove > spaceBelow;
      const maxHeight = Math.max(220, Math.min(430, (openUp ? spaceAbove : spaceBelow)));
      popover.style.width = width + "px";
      popover.style.maxHeight = maxHeight + "px";
      popover.style.left = left + "px";
      popover.style.top = openUp
        ? Math.max(viewportPad, triggerRect.top - gap - Math.min(maxHeight, popover.offsetHeight || maxHeight)) + "px"
        : Math.min(window.innerHeight - viewportPad, triggerRect.bottom + gap) + "px";
      popover.dataset.side = openUp ? "top" : "bottom";
    }

    function positionDetail(anchor) {
      if (!detail || !anchor || detail.hidden) {
        return;
      }
      const gap = 10;
      const viewportPad = 10;
      const anchorRect = anchor.getBoundingClientRect();
      const popoverRect = popover?.getBoundingClientRect();
      const width = Math.min(320, Math.max(230, window.innerWidth - viewportPad * 2));
      detail.style.width = width + "px";
      const detailHeight = detail.offsetHeight || 112;
      const rightSpace = window.innerWidth - anchorRect.right - viewportPad - gap;
      const leftSpace = anchorRect.left - viewportPad - gap;
      let left;
      if (rightSpace >= width || rightSpace >= leftSpace) {
        left = Math.min(anchorRect.right + gap, window.innerWidth - viewportPad - width);
      } else {
        left = Math.max(viewportPad, anchorRect.left - gap - width);
      }
      const minTop = viewportPad;
      const maxTop = window.innerHeight - viewportPad - detailHeight;
      let top = anchorRect.top + (anchorRect.height - detailHeight) / 2;
      if (popoverRect) {
        top = Math.max(top, popoverRect.top);
      }
      top = Math.min(Math.max(minTop, top), Math.max(minTop, maxTop));
      detail.style.left = Math.round(left) + "px";
      detail.style.top = Math.round(top) + "px";
    }

    function moveActive(delta) {
      if (!visibleModels.length) {
        return;
      }
      activeIndex = (activeIndex + delta + visibleModels.length) % visibleModels.length;
      hoveredModel = visibleModels[activeIndex];
      markActiveRow();
      const row = list?.querySelectorAll(".model-row")?.[activeIndex];
      renderDetail(hoveredModel, row);
    }

    function markActiveRow() {
      const rows = Array.from(list?.querySelectorAll(".model-row") || []);
      rows.forEach((row, index) => {
        const active = index === activeIndex;
        row.setAttribute("aria-selected", String(active));
        if (active) {
          setActiveDescendant(row.id);
          row.scrollIntoView({ block: "nearest" });
        }
      });
    }

    function setActiveDescendant(id) {
      trigger?.setAttribute("aria-activedescendant", id);
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
        close();
      }
    });
    trigger?.addEventListener("keydown", (event) => {
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        open();
      } else if (event.key === "Escape") {
        close();
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
    addCustomButton?.addEventListener("click", () => {
      const modelId = cleanModelId(customInput?.value || "");
      if (!modelId) {
        return;
      }
      select(modelId);
      if (customInput) {
        customInput.value = "";
      }
    });

    return { open, close, toggle, applyCatalog, displayName, modelInfo, requestModels };
  }

  function cleanModelId(modelId) {
    return String(modelId || "").trim().replace(/:nitro\s*$/i, "");
  }

  function fallbackModelName(modelId) {
    return String(modelId || "openrouter/auto")
      .replace(/^~/, "")
      .split("/")
      .pop()
      .replace(/-/g, " ")
      .replace(/\blatest\b/gi, "Latest");
  }

  function formatPrice(value) {
    const price = Number(value);
    if (!Number.isFinite(price)) return "unknown";
    if (price < 0.01) return "$" + price.toFixed(4);
    if (price < 1) return "$" + price.toFixed(3);
    return "$" + price.toFixed(2);
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
