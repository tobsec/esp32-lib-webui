(function () {
  Alpine.data("hello", () => ({
    message: "…",
    async load() {
      try {
        const r = await window.WebShell.fetch_json("/api/hello");
        this.message = JSON.stringify(r);
      } catch (e) {
        this.message = "error: " + e.message;
      }
    },
  }));
})();
