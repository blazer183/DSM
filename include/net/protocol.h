#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__cplusplus)
extern "C" {
#endif


#define DSM_PAGE_SIZE 4096
//ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Òªï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ ï¿½ï¿½ï¿½Ä¼ï¿½ï¿½ÐµÄºï¿½ï¿½ï¿½ï¿½ï¿½È«ï¿½ï¿½ï¿½ï¿½caspp page 630-631

/* Constants ------------------------------------------------------------- */
#define RIO_BUFSIZE 8192  /* rioï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ð¡ */
/* Enumerations ---------------------------------------------------------- */

/* Structures ------------------------------------------------------------ */
typedef struct {
    int rio_fd;                /* ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
    int rio_cnt;               /* Î´ï¿½ï¿½ï¿½Ö½ï¿½ï¿½ï¿½ */
    char *rio_bufptr;          /* ï¿½ï¿½Ò»ï¿½ï¿½Î´ï¿½ï¿½ï¿½Ö½Úµï¿½Ö¸ï¿½ï¿½ */
    char rio_buf[RIO_BUFSIZE]; /* ï¿½Ú²ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
} rio_t;
/* Helpers --------------------------------------------------------------- */

/* API ------------------------------------------------------------------- */
void rio_readinit(rio_t *rp, int fd);
ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t rio_readn(rio_t *rp, void *usrbuf, size_t n);



//ï¿½ï¿½ï¿½Ä¹æ·¶


// ================= ï¿½ï¿½Ï¢ï¿½ï¿½ï¿½ï¿½Ã¶ï¿½ï¿½ =================
typedef enum {
<<<<<<< HEAD
    // 1. ï¿½ï¿½Ê¼ï¿½ï¿½ï¿½×¶ï¿½
    DSM_MSG_JOIN_REQ      = 0x01,  // ï¿½Â½Úµï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ë£¬ï¿½ï¿½ÒªÍ³ï¿½Æ½Úµï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½IDï¿½ï¿½È·ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ö·
    DSM_MSG_JOIN_ACK      = 0x02,  // Leaderï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ï¢(ID, ï¿½ï¿½Ö·)ï¿½ï¿½Ò²ï¿½ï¿½ï¿½Çºï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ø°ï¿½ï¿½ï¿½IDï¿½Í»ï¿½Ö·ï¿½ï¿½ï¿½ï¿½Ã¸ï¿½ï¿½Øµï¿½Ç°8ï¿½Ö½ï¿½ï¿½ï¿½IDï¿½ï¿½ï¿½ï¿½8ï¿½Ö½ï¿½ï¿½Ç»ï¿½Ö·ï¿½ï¿½Ä¬ï¿½Ï·ï¿½ï¿½ï¿½0x4000000000
=======
    // 1. ³õÊ¼»¯½×¶Î
    DSM_MSG_JOIN_REQ      = 0x01,  // 
    DSM_MSG_JOIN_ACK      = 0x02,  // 
>>>>>>> trunk

    // 2. Ò³ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ (ï¿½ï¿½ï¿½ï¿½Ð­ï¿½ï¿½)
    DSM_MSG_PAGE_REQ      = 0x10,  // Aï¿½ï¿½Bï¿½ï¿½ï¿½ï¿½Ò³ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
    DSM_MSG_PAGE_REP      = 0x11,  // ×¢ï¿½â£ºBï¿½ï¿½ï¿½ï¿½ï¿½Ð¶ï¿½ï¿½Ô¼ï¿½ï¿½ï¿½prob ownerï¿½ï¿½ï¿½ï¿½real owner,Ö»ï¿½ï¿½ï¿½Ð¶ï¿½ï¿½Ô¼ï¿½ï¿½ï¿½pagetableï¿½ï¿½ï¿½Ó¦Ò³ï¿½ï¿½ownerï¿½Ç·ï¿½Ò»ï¿½Â£ï¿½
                                   // Ò»ï¿½Â¾Í·ï¿½ï¿½ï¿½Ò³ï¿½æ£¬ï¿½ï¿½ï¿½ò·µ»ï¿½Ò³ï¿½ï¿½ownerï¿½ï¿½IDï¿½ï¿½Aï¿½ï¿½ï¿½ï¿½ï¿½Ò³ownerï¿½ï¿½-1ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½0ï¿½Å½ï¿½ï¿½Ìµï¿½ï¿½ï¿½ï¿½ï¿½
       
    // 3. ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
    DSM_MSG_LOCK_ACQ      = 0x20,  // Í¬ï¿½ï¿½
    DSM_MSG_LOCK_REP      = 0x21,  // Í¬ï¿½ï¿½
    
    // 4. Î¬ï¿½ï¿½ï¿½ï¿½È·ï¿½ï¿½
    DSM_MSG_OWNER_UPDATE  = 0x30,  // ï¿½ï¿½ÖªManagerï¿½ï¿½ï¿½ï¿½È¨ï¿½Ñ±ï¿½ï¿½ï¿½ï¿½Í¨ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Î»ï¿½ï¿½ï¿½ï¿½Ò³/ï¿½ï¿½
    DSM_MSG_ACK           = 0xFF   // Í¨ï¿½ï¿½È·ï¿½ï¿½ (ï¿½ï¿½ï¿½ï¿½Í¬ï¿½ï¿½)
} dsm_msg_type_t;

// ================= Í¨ï¿½ï¿½Ð­ï¿½ï¿½Í· (12ï¿½Ö½ï¿½) =================
typedef struct {
    uint8_t  type;           // dsm_msg_type_t
    uint8_t  unused;         // ï¿½ï¿½ï¿½ï¿½Î» (Ò³/ï¿½ï¿½ï¿½Ð¶Ï£ï¿½realowner/dataï¿½Ð¶Ï£ï¿½
    uint16_t src_node_id;    // ï¿½ï¿½ï¿½Í·ï¿½ID (-1/255 ï¿½ï¿½Ê¾Î´Öª)
    uint32_t seq_num;        // ï¿½ï¿½ï¿½Ðºï¿½ (ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½/ï¿½ï¿½ï¿½ï¿½)
    uint32_t payload_len;    // ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ø³ï¿½ï¿½ï¿½ (ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Í·)
} __attribute__((packed)) dsm_header_t;


<<<<<<< HEAD
// [DSM_MSG_JOIN_REQ]
typedef struct {
    uint16_t listen_port;    // ï¿½ï¿½ï¿½ï¿½ Leader ï¿½ï¿½ï¿½ï¿½ï¿½Ä¸ï¿½ï¿½Ë¿Ú¼ï¿½ï¿½ï¿½
=======




// ================= ÏêÏ¸¸ºÔØ¶¨Òå (Payloads) =================

// ---------------- A. ³õÊ¼»¯Ä£¿é ----------------

// [DSM_MSG_JOIN_REQ] ¼ÓÈëÇëÇó
// ¸ºÔØ£ºÍ¨³£Îª¿Õ£¬»òÕßÊÇ×ÔÉíµÄ¼àÌý¶Ë¿Ú
typedef struct {
    uint16_t listen_port;    // ¸æËß Leader ÎÒÔÚÄÄ¸ö¶Ë¿ÚÌý
>>>>>>> trunk
} __attribute__((packed)) payload_join_req_t;


// [DSM_MSG_JOIN_ACK]
typedef struct {
<<<<<<< HEAD
    uint16_t assigned_node_id;  // ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ëµï¿½ ID
    uint16_t node_count;        // ï¿½Ü½Úµï¿½ï¿½ï¿½ (ï¿½ï¿½ï¿½ï¿½ Hash ï¿½ï¿½ï¿½ï¿½)
    uint64_t dsm_mem_size;      // ï¿½ï¿½ï¿½ï¿½ï¿½Ú´ï¿½ï¿½Ü´ï¿½Ð¡
=======
    uint16_t assigned_node_id;  // ·ÖÅä¸øÐÂÈËµÄ ID
    uint16_t node_count;        // ×Ü½ÚµãÊý (ÓÃÓÚ Hash ¼ÆËã)
    
>>>>>>> trunk
} __attribute__((packed)) payload_join_ack_t;


// ---------------- B. Ò³ï¿½ï¿½ï¿½ï¿½ï¿½Ä£ï¿½ï¿½ ----------------

// [DSM_MSG_PAGE_REQ] Requestor -> Manager
typedef struct {
    uint32_t page_index;        // ï¿½ï¿½ï¿½ï¿½ï¿½È«ï¿½ï¿½Ò³ï¿½ï¿½
} __attribute__((packed)) payload_page_req_t;

// [DSM_MSG_PAGE_REP] Manager -> RealOwner
// ï¿½ï¿½ï¿½å£º"Node X ï¿½ï¿½Òª page_indexï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½â£¬ï¿½ã·¢ï¿½ï¿½ï¿½ï¿½"
typedef struct {
    uint32_t page_index;        // Ò³ï¿½ï¿½
    uint16_t requester_id;      // ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Òªï¿½ï¿½ï¿½ÝµÄ½Úµï¿½ID (RealOwnerÒªï¿½ï¿½ï¿½ï¿½ï¿½Ý·ï¿½ï¿½ï¿½ï¿½ï¿½)
} __attribute__((packed)) payload_page_rep_t;

// [DSM_MSG_PAGE_DATA] RealOwner -> Requestor
// ï¿½ï¿½ï¿½ï¿½ï¿½Í¨ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Òª struct ï¿½ï¿½ï¿½å£¬Ö±ï¿½ï¿½ Header + 4096ï¿½Ö½ï¿½ï¿½ï¿½ï¿½ï¿½
// ï¿½ï¿½ï¿½ï¿½ï¿½ÒªÔªï¿½ï¿½ï¿½Ý£ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ unused ï¿½Ö¶ï¿½


// ---------------- C. ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ä£ï¿½ï¿½ ----------------

// [DSM_MSG_LOCK_ACQ] / [DSM_MSG_LOCK_REL] Requestor -> Manager
typedef struct {
    uint32_t lock_id;           // ï¿½ï¿½ ID
} __attribute__((packed)) payload_lock_req_t;

// [DSM_MSG_LOCK_REP] Manager -> Requestor (ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½)
typedef struct {
    uint32_t lock_id;
    uint32_t invalid_set_count; // Scope Consistency: ï¿½ï¿½ÒªÊ§Ð§ï¿½ï¿½Ò³ï¿½ï¿½ï¿½ï¿½
    // ï¿½ï¿½ï¿½ï¿½ï¿½Åºï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ invalid_set ï¿½Ð±ï¿½
    uint32_t realowner;

} __attribute__((packed)) payload_lock_rep_t;


// ---------------- D. Î¬ï¿½ï¿½ï¿½ï¿½È·ï¿½ï¿½ ----------------

// [DSM_MSG_OWNER_UPDATE] RealOwner -> Manager
typedef struct {
    uint32_t resource_id;    // Ò³ï¿½Å»ï¿½ï¿½ï¿½ID
    uint16_t new_owner_id;   // ï¿½ï¿½Ô´ï¿½ï¿½ï¿½Ú¹ï¿½Ë­ï¿½ï¿½
} __attribute__((packed)) payload_owner_update_t;

// [DSM_MSG_ACK]
typedef struct {
    uint32_t target_seq;     // È·ï¿½ï¿½ï¿½Ç¶ï¿½ï¿½Ä¸ï¿½ï¿½ï¿½ï¿½ï¿½Ä»ï¿½Ó¦
    uint8_t  status;         // 0=OK, 1=Fail
} __attribute__((packed)) payload_ack_t;


#if defined(__cplusplus)
}
#endif

#endif /* NET_PROTOCOL_H */
