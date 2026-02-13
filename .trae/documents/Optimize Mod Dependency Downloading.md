I will modify the `ModDownloadPage` class to prevent automatic updates of existing dependency mods when downloading a modpack or mod.

**Changes:**

1.  **Modify `ModDownloadPage.h`**:
    *   Update the `buildDownloadQueue` method signature to accept a `bool isDependency` parameter, defaulting to `false`.

2.  **Modify `ModDownloadPage.cpp`**:
    *   Update the implementation of `buildDownloadQueue` to use the `isDependency` parameter.
    *   In `buildDownloadQueue`, logic will be added to check if `isDependency` is true.
    *   If `isDependency` is true and the mod status is `MOD_NEEDS_UPDATE` (meaning a version is already installed), the download will be skipped for that item.
    *   Update the recursive call to `buildDownloadQueue` within the dependency loop to pass `true` for `isDependency`.

**Reasoning:**
Currently, `buildDownloadQueue` fetches the latest version of every dependency. If a dependency is already installed but is an older version (or just different file ID), `getModInstallStatus` returns `MOD_NEEDS_UPDATE`, causing the launcher to download the latest version. By distinguishing between the main mod (user-initiated) and dependencies, we can skip the update for dependencies that are already present, respecting the user's existing setup and saving bandwidth/time.
