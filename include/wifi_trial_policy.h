#pragma once
// Radio-independent failure policy. An unreachable saved network must remain the
// retry target; a failed candidate is never promoted merely to escape recovery.
enum class WifiTrialRecovery { ReconnectSaved, RetrySavedInPortal, StayInPortal };
inline WifiTrialRecovery wifi_trial_recovery(bool has_saved_network,bool portal_up) {
    if(!has_saved_network) return WifiTrialRecovery::StayInPortal;
    return portal_up ? WifiTrialRecovery::RetrySavedInPortal : WifiTrialRecovery::ReconnectSaved;
}
inline bool wifi_trial_has_address(bool associated,bool nonzero_address) {return associated && nonzero_address;}
