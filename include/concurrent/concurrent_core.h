#ifndef DSM_STATE_HPP
#define DSM_STATE_HPP

#include <mutex>
#include <memory>
#include <iostream>
#include <condition_variable> 
#include <vector>
#include "os/page_table.h"
<<<<<<< HEAD
#include "os/lock_table.h"  // æ³¨æ„æ–‡ä»¶åå¤§å°å†™è¦å’Œå®žé™…æ–‡ä»¶ä¸€è‡´
=======
#include "os/lock_table.h"  // ×¢ÒâÎÄ¼þÃû´óÐ¡Ð´ÒªºÍÊµ¼ÊÎÄ¼þÒ»ÖÂ
>>>>>>> trunk
#include "os/bind_table.h"
#include "os/table_base.hpp"
#include "net/protocol.h" 

// [0x01] DSM_MSG_JOIN_REQ
<<<<<<< HEAD
// æŽ¥æ”¶è€…ï¼šManager (Leader)
// ä½œç”¨ï¼šè®°å½•æ–°èŠ‚ç‚¹ï¼Œåˆ†é…IDï¼Œå‡†å¤‡å›žå¤ ACK
void process_join_req(int sock, const dsm_header_t& head, const payload_join_req_t& body);

// [0x10] DSM_MSG_PAGE_REQ
// æŽ¥æ”¶è€…ï¼šManager æˆ– Owner
// ä½œç”¨ï¼š
// 1. å¦‚æžœæˆ‘æ˜¯ Ownerï¼šç›´æŽ¥å‘å›ž DSM_MSG_PAGE_REP (å¸¦æ•°æ®, unused=1)
// 2. å¦‚æžœæˆ‘ä¸æ˜¯ï¼šå‘å›ž DSM_MSG_PAGE_REP (å¸¦é‡å®šå‘ID, unused=0)
void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body);

// [0x20] DSM_MSG_LOCK_ACQ
// æŽ¥æ”¶è€…ï¼šManager
// ä½œç”¨ï¼šæŸ¥ LockTableï¼Œå¦‚æžœç©ºé—²åˆ™æŽˆäºˆ (å‘LOCK_REP)ï¼Œå¦‚æžœå ç”¨åˆ™åŠ å…¥é˜Ÿåˆ—
void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body);

// [0x30] DSM_MSG_OWNER_UPDATE
// æŽ¥æ”¶è€…ï¼šManager
// ä½œç”¨ï¼šæ”¶åˆ° RealOwner çš„é€šçŸ¥ï¼Œæ›´æ–° Directory ä¸­çš„ owner_id
void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body);

// =========================================================================
// 2. ç›‘å¬æœåŠ¡å…¥å£ (Daemon)
=======
// ½ÓÊÕÕß£ºnode 0
// ×÷ÓÃ£º¼ÇÂ¼ÐÂ½Úµã£¬·ÖÅäID£¬×¼±¸»Ø¸´ ACK
void process_join_req(int sock, const dsm_header_t& head, const payload_join_req_t& body);

// [0x10] DSM_MSG_PAGE_REQ
// ½ÓÊÕÕß£ºprobOwner/realOwner
// ×÷ÓÃ£º
// 1. Èç¹ûÎÒÊÇ Owner£ºÖ±½Ó·¢»Ø DSM_MSG_PAGE_REP (´øÊý¾Ý, unused=1)
// 2. Èç¹ûÎÒ²»ÊÇ£º·¢»Ø DSM_MSG_PAGE_REP (´øÖØ¶¨ÏòID, unused=0)
void process_page_req(int sock, const dsm_header_t& head, const payload_page_req_t& body);

// [0x20] DSM_MSG_LOCK_ACQ
// ½ÓÊÕÕß£ºprobOwner/realOwner
// ×÷ÓÃ£ºÍ¬ÉÏ
void process_lock_acq(int sock, const dsm_header_t& head, const payload_lock_req_t& body);

// [0x30] DSM_MSG_OWNER_UPDATE
// ½ÓÊÕÕß£ºprobOwner
// ×÷ÓÃ£ºÊÕµ½ÐÂµÄ RealOwner µÄÍ¨Öª£¬¸üÐÂ Directory ÖÐµÄ owner_id
void process_owner_update(int sock, const dsm_header_t& head, const payload_owner_update_t& body);

// =========================================================================
// 2. ¼àÌý·þÎñÈë¿Ú (Daemon)
>>>>>>> trunk
// =========================================================================
void dsm_start_daemon(int port);
void peer_handler(int connfd); 


#endif // DSM_STATE_HPP