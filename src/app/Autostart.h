#pragma once
// Autostart via HKCU\Software\Microsoft\Windows\CurrentVersion\Run\CloudSpotlight = "\"<exe>\" --background".
// Respects the Task Manager "Startup apps" switch (Explorer\StartupApproved\Run).
namespace cs::autostart {

// Run value present AND not disabled in Task Manager.
bool IsEnabled();
// Writes the Run value for the current exe and clears a Task Manager "disabled" mark (explicit user intent).
bool Enable();
bool Disable();
// Startup reconciliation with cfg.autostart. When enabled: (re)writes the Run value if missing or pointing to an
// old exe location, but keeps a Task Manager "disabled" choice. When disabled: removes our Run value.
void Sync(bool wanted);

}  // namespace cs::autostart
