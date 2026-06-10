// No project-specific Alpine components needed — the panels above are
// built entirely out of webui.poll + webui.components.logViewer. Kept
// as an empty file so the existing /panels.js route doesn't 404 and
// so consumers have a template to drop bespoke Alpine.data(...) into
// when the factories aren't enough.
//
// Example of when you'd want this file: an OTA-upload form whose state
// machine (file picked → signing-key check → progress bar → success
// vs probe-phase rollback warning) is too specific to be a generic
// factory. Define it as Alpine.data("my_ota_form", () => ({...})) here
// and reference it from panels.html via x-data="my_ota_form".
