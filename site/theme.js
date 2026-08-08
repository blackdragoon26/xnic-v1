(() => {
  const storageKey = "xnic-theme";
  const root = document.documentElement;
  const media = window.matchMedia("(prefers-color-scheme: dark)");

  function storedTheme() {
    try {
      const value = window.localStorage.getItem(storageKey);
      return value === "light" || value === "dark" ? value : null;
    } catch {
      return null;
    }
  }

  function setTheme(theme, persist) {
    root.dataset.theme = theme;
    root.style.colorScheme = theme;

    const themeColor = document.querySelector('meta[name="theme-color"]');
    if (themeColor) {
      themeColor.content = theme === "dark" ? "#151617" : "#faf9f6";
    }

    const toggle = document.querySelector("[data-theme-toggle]");
    if (toggle) {
      const dark = theme === "dark";
      toggle.setAttribute("aria-pressed", String(dark));
      toggle.setAttribute("aria-label", dark ? "Use light theme" : "Use dark theme");
      toggle.querySelector("[data-theme-label]").textContent = dark ? "Light" : "Dark";
    }

    if (persist) {
      try {
        window.localStorage.setItem(storageKey, theme);
      } catch {
        // The selected theme still applies for this page when storage is blocked.
      }
    }
  }

  setTheme(storedTheme() || (media.matches ? "dark" : "light"), false);

  window.addEventListener("DOMContentLoaded", () => {
    setTheme(root.dataset.theme, false);
    document.querySelector("[data-theme-toggle]").addEventListener("click", () => {
      setTheme(root.dataset.theme === "dark" ? "light" : "dark", true);
    });

    media.addEventListener("change", (event) => {
      if (!storedTheme()) {
        setTheme(event.matches ? "dark" : "light", false);
      }
    });
  });
})();
