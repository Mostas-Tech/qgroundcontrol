--[[ 
  Flight Authority Arming Gate (MAVLink-driven)
  - Listens for MAV_CMD_DO_SEND_SCRIPT_MESSAGE (217) via raw MAVLink decode
  - Param1 = SUB_ID (accept/reject sub-id)
  - Accept sub-id: Param2 = 1 allow, Param3 = validity window (seconds)
  - Reject sub-id: Param2 = 1 allow (soft) for Param3 seconds with warning
                  Param2 = 0 deny, Param3 = registered flag
                  1 = registered but no flight permission, 0 = not registered.

  ]]