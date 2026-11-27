// Prepare the SDK state normally set by a successful asynchronous Unicode
// query. The test injects WM_COPYDATA replies without contacting a live index.
#include "../../third_party/everything-sdk-v2/src/Everything.c"

void PrepareUnicodeQuery2(DWORD reply_id)
{
    Everything_Reset();
    Everything_SetReplyID(reply_id);
    _Everything_IsUnicodeQuery = TRUE;
    _Everything_QueryVersion = 2;
}
