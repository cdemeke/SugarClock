#pragma once
// The journal is a single atomic NVS value. Legacy keys are mirrored only after
// it exists; a failed mirror/cleanup retains the redo record for next boot.
enum class ConfigCommit { Saved, Rejected, RecoveryPending };
template<class Journal,class Mirror,class Finish>
ConfigCommit config_transaction(Journal journal,Mirror mirror,Finish finish) {
    if(!journal()) return ConfigCommit::Rejected;
    if(!mirror()) return ConfigCommit::RecoveryPending;
    if(!finish()) return ConfigCommit::RecoveryPending;
    return ConfigCommit::Saved;
}
