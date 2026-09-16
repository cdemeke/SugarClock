// Arduino calls this hook before setup(). Its default implementation accepts a
// pending OTA image immediately, before SugarClock's health checks can run.
// Keep the image pending so ota_manager can validate it after its health window,
// or the bootloader can roll back if the application restarts before acceptance.
// C linkage is required to override Arduino's weak C implementation.
extern "C" bool verifyRollbackLater() {
    return true;
}
