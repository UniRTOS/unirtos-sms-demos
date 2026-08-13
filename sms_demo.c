/*****************************************************************/ /**
* @file sms_demo.c
* @brief
* @author liaz.liao@quectel.com
* @date 2025-10-29
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-10-29 <td>1.0 <td>liaz.liao <td> Init
* </table>
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_datacall.h"
#include "qosa_sms.h"
#include "qosa_system_utils.h"
#include "qcm_utils.h"
#include "qosa_at_config.h"
#include "qosa_center_task.h"
#include "qosa_event_notify.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG                       LOG_TAG_DEMO

// SMS demo test task control macro
#define QOSA_SMS_DEMO_TEST_TASK_ENABLE    0 /*!< Enable SMS demo test task: 1-enable, 0-disable */

// SMS configuration parameters
#define QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT 300       /*!< Network registration timeout (seconds) */
#define QOSA_SMS_DEMO_SMS_MAX_LENGTH      (5 * 160) /*!< Maximum SMS length */

/**
 * @enum qosa_sms_demo_format_type_e
 * @brief SMS format type enumeration
 */
typedef enum
{
    QOSA_SMS_DEMO_FORMAT_PDU = 0, /*!< PDU format */
    QOSA_SMS_DEMO_FORMAT_TEXT,    /*!< Text format */
} qosa_sms_demo_format_type_e;

/**
 * @enum qosa_sms_demo_msg_type_e
 * @brief SMS demo message type enumeration
 */
typedef enum
{
    SMS_DEMO_MSG_SEND_SMS = 0, /*!< Send SMS */
    SMS_DEMO_MSG_READ_SMS,     /*!< Read SMS */
    SMS_DEMO_MSG_DELETE_SMS,   /*!< Delete SMS */
    SMS_DEMO_MSG_LIST_SMS,     /*!< List SMS */
    SMS_DEMO_MSG_EXIT          /*!< Exit program */
} qosa_sms_demo_msg_type_e;

/**
 * @struct qosa_sms_demo_msg_t
 * @brief SMS demo message structure
 */
typedef struct
{
    qosa_sms_demo_msg_type_e msg_type; /*!< Message type */
    void                    *data;     /*!< Message data pointer */
    size_t                   data_len; /*!< Message data length */
} qosa_sms_demo_msg_t;

/**
 * @struct qosa_sms_demo_send_msg_t
 * @brief SMS sending data structure
 */
typedef struct
{
    char          phone_number[QOSA_ADDRESS_MAX_LEN * 4 + 1]; /*!< Phone number */
    char          message[QOSA_SMS_DEMO_SMS_MAX_LENGTH + 1];  /*!< SMS content */
    qosa_uint32_t pdu_length;                                 /*!< PDU length */
} qosa_sms_demo_send_msg_t;

/**
 * @struct qosa_sms_demo_delete_msg_t
 * @brief SMS deletion data structure
 */
typedef struct
{
    qosa_uint16_t      index;   /*!< SMS index */
    qosa_sms_delflag_e delflag; /*!< Delete flag */
} qosa_sms_demo_delete_msg_t;

/**
 * @struct qosa_sms_demo_sms_async_ctx_t
 * @brief SMS asynchronous operation context structure
 */
typedef struct
{
    qosa_sms_demo_format_type_e format;   /*!< format of the message */
    qosa_sms_stor_e             storage;  /*!< storage to read message */
    qosa_uint16_t              *list;     /*!< record index list */
    qosa_uint16_t               list_len; /*!< record index list length */
    qosa_uint16_t               cur_pos;  /*!< current position in the record index list */
} qosa_sms_demo_sms_async_ctx_t;

/**
 * @struct qosa_sms_demo_mem_info_t
 * @brief SMS storage information structure
 */
typedef struct
{
    qosa_sms_stor_e mem1; /*!< Memory for read and delete operations */
    qosa_sms_stor_e mem2; /*!< Memory for write and send operations */
    qosa_sms_stor_e mem3; /*!< Memory for received messages */
} qosa_sms_demo_mem_info_t;

/**
 * @struct qosa_sms_demo_config_t
 * @brief SMS demo configuration structure
 */
typedef struct
{
    qosa_sms_demo_format_type_e format[CONFIG_QOSA_SIM_HAL_SLOT_NUM];    /*!< format of the message. */
    qosa_sms_demo_mem_info_t    mem[CONFIG_QOSA_SIM_HAL_SLOT_NUM];       /*!< memory storage information for application. */
    qosa_bool_t                 mem_ready[CONFIG_QOSA_SIM_HAL_SLOT_NUM]; /*!< memory storage is ready or not. */
} qosa_sms_demo_config_t;

/**
 * @struct qosa_sms_demo_report_t
 * @brief SMS module report message structure
 */
typedef struct
{
    qosa_uint32_t cmd; /*!< Command type */
    union
    {
        qosa_sms_init_status_event_t init_status_event; /*!< Initialization status event */
    };
} qosa_sms_demo_report_t;

/*===========================================================================
 *  Variable definitions
 ===========================================================================*/

/** @brief SMS demo task handle */
qosa_task_t g_sms_demo_task_ptr = QOSA_NULL;

#if QOSA_SMS_DEMO_TEST_TASK_ENABLE
/** @brief SMS function test handle */
qosa_task_t g_sms_demo_test_task_ptr = QOSA_NULL;
#endif /* QOSA_SMS_DEMO_TEST_TASK_ENABLE */

/** @brief SMS demo message queue handle */
qosa_msgq_t g_sms_demo_msgq = QOSA_NULL;

/** @brief SMS configuration global variable */
static qosa_sms_demo_config_t g_sms_cfg = {0};
/** @brief Current SIM card ID */
static qosa_uint8_t g_sms_simid = 0;
/** @brief  SMS messages character */
static qosa_at_chset_e g_sms_messages_chset = QOSA_CS_GSM;

/** @brief SMS status value string mapping table */
static const qosa_value_str_map_t g_sms_status_vs[] = {
    {QOSA_SMS_UNREAD, "REC UNREAD"}, /*!< Unread SMS status - received but not read */
    {QOSA_SMS_READ, "REC READ"},     /*!< Read SMS status - received and read */
    {QOSA_SMS_UNSENT, "STO UNSENT"}, /*!< Unsent SMS status - stored in device but not sent */
    {QOSA_SMS_SENT, "STO SENT"},     /*!< Sent SMS status - successfully delivered to network */
    {QOSA_SMS_ALL, "ALL"},           /*!< All SMS status - includes all message types */
    {0, QOSA_NULL},
};

/*===========================================================================
 *  Static function declarations
 ===========================================================================*/

/**
 * @brief Initialize SMS configuration with default values
 *
 * @return void
 * @note
 */
static void qosa_sms_demo_init_config(void);

/**
 * @brief SMS status event handler function
 *
 * @param[in] event
 *          - SMS initialization status event
 * @return void
 * @note
 */
static void qosa_sms_demo_sms_status_handler(qosa_sms_init_status_event_t *event);

/**
 * @brief Handle SIM module reported ind events
 *
 * @param[in] cmd
 *          - Command event
 * @param[in] user_data_ptr
 *          - User data pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_sms_indicate_hdlr(qosa_center_cmd_event_enum_e cmd, void *user_data_ptr);

/**
 * @brief Modem SMS status event callback function
 *
 * @param[in] user_argv
 *          - User parameter
 * @param[in] argv
 *          - Event parameter
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int unirtos_modem_sms_status_event_cb(void *user_argv, void *argv);

/**
 * @brief Initialize SIM card demo event callback function
 *
 * @return void
 * @note This function is used to register SIM card status related event callback handlers,
 * including SIM card status change events and SIM card insertion/removal status events.
 */
static void qosa_sms_demo_event_cb_init(void);

/**
 * @brief Send SMS response callback function
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameters
 * @return void
 * @note
 */
static void qosa_sms_demo_send_msg_rsp(void *ctx, void *argv);

/**
 * @brief List SMS response callback function
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameters
 * @return void
 * @note
 */
static void qosa_sms_demo_list_msg_rsp(void *ctx, void *argv);

/**
 * @brief Read SMS response callback function
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameters
 * @return void
 * @note
 */
static void qosa_sms_demo_read_msg_rsp(void *ctx, void *argv);

/**
 * @brief Delete SMS response callback function
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameters
 * @return void
 * @note
 */
static void qosa_sms_demo_delete_msg_rsp(void *ctx, void *argv);

/**
 * @brief Send SMS function
 *
 * @param[in] phone_number
 *          - Target phone number
 * @param[in] message_txt
 *          - SMS content
 * @param[in] pdu_length
 *          - PDU length
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_send_sms(const char *phone_number, const char *message_txt, qosa_uint32_t pdu_length);

/**
 * @brief List SMS function
 *
 * @param[in] stat
 *          - SMS status filter condition
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_list_sms(const char *stat);

/**
 * @brief Read SMS function
 *
 * @param[in] index
 *          - SMS index
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_read_sms(qosa_uint16_t index);

/**
 * @brief Delete SMS function
 *
 * @param[in] index
 *          - SMS index
 * @param[in] delflag
 *          - Delete flag
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_delete_sms(qosa_uint16_t index, qosa_sms_delflag_e delflag);

/**
 * @brief Handle send SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_send_sms(qosa_sms_demo_msg_t *msg);

/**
 * @brief Handle read SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_read_sms(qosa_sms_demo_msg_t *msg);

/**
 * @brief Handle delete SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_delete_sms(qosa_sms_demo_msg_t *msg);

/**
 * @brief Handle list SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_list_sms(qosa_sms_demo_msg_t *msg);

/**
 * @brief Initialize SMS list context
 *
 * @param[in] simid
 *          - SIM card ID
 * @param[in] storage
 *          - SMS storage location
 * @param[in] status
 *          - SMS status
 * @return qosa_sms_demo_sms_async_ctx_t*
 *          - Success returns context pointer, failure returns NULL
 * @note
 */
static qosa_sms_demo_sms_async_ctx_t *qosa_sms_demo_list_sms_init(qosa_uint8_t simid, qosa_sms_stor_e storage, qosa_sms_status_e status);

/**
 * @brief Get SMS storage information
 *
 * @param[in] simid
 *          - SIM card ID
 * @param[in] mem_info
 *          - Storage information structure pointer
 * @return int
 *          - Success returns QOSA_SMS_SUCCESS, failure returns error code
 * @note
 */
static int qosa_sms_demo_get_mem(qosa_uint8_t simid, qosa_sms_demo_mem_info_t *mem_info);

/**
 * @brief Get next SMS record index
 *
 * @param[in] ctx
 *          - Context pointer
 * @return int
 *          - Success returns index value, failure returns -1
 * @note
 */
static int qosa_sms_demo_list_sms_fetch_next_ex(qosa_sms_demo_sms_async_ctx_t *ctx);

/**
 * @brief Free SMS list context
 *
 * @param[in] ctx
 *          - Context pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_delete_sms_deinit_ex(qosa_sms_demo_sms_async_ctx_t *ctx);

/**
 * @brief Get SMS status string
 *
 * @param[in] status
 *          - SMS status value
 * @return const char*
 *          - Status string
 * @note
 */
static const char *qosa_sms_demo_sms_msg_status_str(qosa_uint8_t status);

/*===========================================================================
 *  Function declarations
 ===========================================================================*/

/**
 * @brief Set SIM card ID
 *
 * @param[in] simid
 *          - SIM card ID
 * @return void
 * @note
 */
void qosa_sms_set_sim_id(qosa_uint8_t simid);

/**
 * @brief Get current SIM card ID
 *
 * @return qosa_uint8_t
 *          - Current SIM card ID
 * @note
 */
qosa_uint8_t qosa_sms_get_sim_id(void);

/**
 * @brief Set SMS format for specific SIM
 *
 * @param[in] format
 *          - SMS format (PDU or TEXT)
 * @return void
 * @note
 */
void qosa_sms_set_format(qosa_sms_demo_format_type_e format);

/**
 * @brief Set SMS memory configuration for specific SIM
 *
 * @param[in] mem1
 *          - Memory for read and delete operations
 * @param[in] mem2
 *          - Memory for write and send operations
 * @param[in] mem3
 *          - Memory for received messages
 * @return void
 * @note
 */
void qosa_sms_set_memory_config(qosa_sms_stor_e mem1, qosa_sms_stor_e mem2, qosa_sms_stor_e mem3);

/**
 * @brief Get current SMS configuration for specific SIM
 *
 * @param[in] format
 *          - Pointer to store format value
 * @param[in] mem_info
 *          - Pointer to store memory configuration
 * @param[in] ready
 *          - Pointer to store memory ready status
 * @return void
 * @note
 */
void qosa_sms_get_config_ex(qosa_sms_demo_format_type_e *format, qosa_sms_demo_mem_info_t *mem_info, qosa_bool_t *ready);

/**
 * @brief Set SMS center number
 *
 * @param[in] sca_str
 *          - SMS center number string
 * @param[in] tosca_type
 *          - Address type
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
int qosa_sms_set_center_number(const char *sca_str, qosa_uint8_t tosca_type);

/**
 * @brief Get SMS center number
 *
 * @param[in] sca_buf
 *          - Buffer to store SMS center number
 * @param[in] buf_len
 *          - Buffer length
 * @param[in] tosca_type
 *          - Pointer to store address type
 * @return void
 * @note
 */
void qosa_sms_get_center_number(char *sca_buf, qosa_uint8_t buf_len, qosa_uint8_t *tosca_type);

/**
 * @brief Set SMS messages character encoding format
 *
 * @param[in] data_chset
 *          - SMS messages character encoding format
 * @return void
 * @note
 */

void qosa_sms_set_messages_chset(qosa_at_chset_e data_chset);

/**
 * @brief Get SMS messages character encoding format
 *
* @return qosa_at_chset_e
 *          - Current SMS messages character encoding format
 * @note
 */
qosa_at_chset_e qosa_sms_get_messages_chset(void);

/**
 * @brief Send Short Message Service (SMS) API
 *
 * @param[in] phone_number
 *          - Destination phone number string
 * @param[in] message
 *          - SMS message content
 * @param[in] pdu_length
 *          - Protocol Data Unit length (set to 0 for text format SMS)
 * @return int
 *          - Success returns 0, failure returns -1
 * @note For text SMS, set pdu_length to 0; for PDU format, provide actual PDU length
 */
int qosa_sms_send_message(const char *phone_number, const char *message, const qosa_uint32_t pdu_length);

/**
 * @brief Read Short Message Service (SMS) API
 *
 * @param[in] index
 *          - Index of the SMS message to read
 * @return int
 *          - Success returns 0, failure returns -1
 * @note The index refers to the position of the SMS in the storage
 */
int qosa_sms_read_message(qosa_uint16_t index);

/**
 * @brief Delete Short Message Service (SMS) API
 *
 * @param[in] index
 *          - Index of the SMS message to delete
 * @param[in] delflag
 *          - Delete flag
 * @return int
 *          - Success returns 0, failure returns -1
 * @note The index specifies which SMS message to remove from storage
 */
int qosa_sms_delete_message(qosa_uint16_t index, qosa_sms_delflag_e delflag);

/**
 * @brief List Short Message Service (SMS) API
 *
 * @param[in] stat
 *          - SMS status filter condition (e.g., "ALL", "REC UNREAD", "REC READ")
 * @return int
 *          - Success returns 0, failure returns -1
 * @note Filter messages by status: ALL, REC UNREAD, REC READ, STO UNSENT, STO SENT
 */
int qosa_sms_list_messages(const char *stat);

/**
 * @brief Stop SMS demonstration program
 *
 * @return void
 * @note This function terminates the SMS demo execution and releases related resources
 */
void qosa_sms_stop(void);

#if QOSA_SMS_DEMO_TEST_TASK_ENABLE
/**
 * @brief SMS demonstration test function
 *
 * @return void
 * @note This function performs comprehensive testing of SMS functionality including sending, reading, listing and deleting messages
 */
void qosa_sms_demo_test_example(void);

/**
 * @brief SMS demonstration test task
 *
 * @param[in] arg
 *          - Task parameter pointer (unused)
 * @return void
 * @note
 */
void qosa_sms_demo_test_task(void *arg);
#endif /* QOSA_SMS_DEMO_TEST_TASK_ENABLE */

/**
 * @brief Main function for SMS demonstration task
 *
 * @param[in] arg
 *          - Task parameter pointer (unused)
 * @return void
 * @note Initializes event callbacks and enters main demo loop
 */
void qosa_sms_demo_task(void *arg);

/*===========================================================================
 *  Function implementations
 ===========================================================================*/

/**
 * @brief Set SIM card ID
 *
 * @param[in] simid
 *          - SIM card ID
 * @return void
 * @note
 */
void qosa_sms_set_sim_id(qosa_uint8_t simid)
{
    if (simid < CONFIG_QOSA_SIM_HAL_SLOT_NUM)
    {
        g_sms_simid = simid;
    }

    QLOGI("SIM ID for setting: %d", simid);
    return;
}

/**
 * @brief Get current SIM card ID
 *
 * @return qosa_uint8_t
 *          - Current SIM card ID
 * @note
 */
qosa_uint8_t qosa_sms_get_sim_id(void)
{
    return g_sms_simid;
}

/**
 * @brief Set SMS format for specific SIM
 *
 * @param[in] format
 *          - SMS format (PDU or TEXT)
 * @return void
 * @note
 */
void qosa_sms_set_format(qosa_sms_demo_format_type_e format)
{
    g_sms_cfg.format[g_sms_simid] = format;
    QLOGI("Set SMS format for SIM %d to %s", g_sms_simid, ((format == QOSA_SMS_DEMO_FORMAT_PDU) ? "PDU" : "TEXT"));

    return;
}

/**
 * @brief Set SMS memory configuration for specific SIM
 *
 * @param[in] mem1
 *          - Memory for read and delete operations
 * @param[in] mem2
 *          - Memory for write and send operations
 * @param[in] mem3
 *          - Memory for received messages
 * @return void
 * @note
 */
void qosa_sms_set_memory_config(qosa_sms_stor_e mem1, qosa_sms_stor_e mem2, qosa_sms_stor_e mem3)
{
    qosa_sms_cfg_t sms_conf = {0};

    // Validate parameter range
    if (mem1 > QOSA_SMS_STOR_SM || mem2 > QOSA_SMS_STOR_SM || mem3 > QOSA_SMS_STOR_SM)
    {
        QLOGI("Invalid memory config parameter: mem1=%d, mem2=%d, mem3=%d", mem1, mem2, mem3);
        return;
    }

    g_sms_cfg.mem[g_sms_simid].mem1 = mem1;
    g_sms_cfg.mem[g_sms_simid].mem2 = mem2;
    g_sms_cfg.mem[g_sms_simid].mem3 = mem3;
    QLOGI("Set memory config for SIM %d: mem1=%d, mem2=%d, mem3=%d", g_sms_simid, mem1, mem2, mem3);

    // Update configuration to platform layer
    qosa_sms_get_config(g_sms_simid, &sms_conf);

    sms_conf.mem1 = mem1;
    sms_conf.mem2 = mem2;
    sms_conf.mem3 = mem3;
    qosa_sms_set_config(g_sms_simid, &sms_conf);

    return;
}

/**
 * @brief Get current SMS configuration for specific SIM
 *
 * @param[in] format
 *          - Pointer to store format value
 * @param[in] mem_info
 *          - Pointer to store memory configuration
 * @param[in] ready
 *          - Pointer to store memory ready status
 * @return void
 * @note
 */
void qosa_sms_get_config_ex(qosa_sms_demo_format_type_e *format, qosa_sms_demo_mem_info_t *mem_info, qosa_bool_t *ready)
{
    if (format != QOSA_NULL)
    {
        *format = g_sms_cfg.format[g_sms_simid];
    }

    if (mem_info != QOSA_NULL)
    {
        *mem_info = g_sms_cfg.mem[g_sms_simid];
    }

    if (ready != QOSA_NULL)
    {
        *ready = g_sms_cfg.mem_ready[g_sms_simid];
    }

    return;
}

/**
 * @brief Set SMS center number
 *
 * @param[in] sca_str
 *          - SMS center number string
 * @param[in] tosca_type
 *          - Address type
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
int qosa_sms_set_center_number(const char *sca_str, qosa_uint8_t tosca_type)
{
    qosa_uint8_t            tosca = 0;
    qosa_sms_address_info_t address = {0};

    // Parameter check
    if (sca_str == QOSA_NULL || qosa_strlen(sca_str) == 0)
    {
        return -1;
    }

    if (tosca_type == 0)
    {
        tosca = 129;  //Address type, default 129
    }
    else
    {
        tosca = tosca_type;
    }

    // Convert SMS center number string to address structure
    if (QOSA_SMS_SUCCESS != qosa_sms_text_to_address((char *)sca_str, qosa_strlen(sca_str), tosca, &address))
    {
        return -1;
    }

    // Set SMS center number
    if (QOSA_SMS_SUCCESS != qosa_sms_set_sca(g_sms_simid, &address))
    {
        return -1;
    }

    return 0;
}

/**
 * @brief Get SMS center number
 *
 * @param[in] sca_buf
 *          - Buffer to store SMS center number
 * @param[in] buf_len
 *          - Buffer length
 * @param[in] tosca_type
 *          - Pointer to store address type
 * @return void
 * @note
 */
void qosa_sms_get_center_number(char *sca_buf, qosa_uint8_t buf_len, qosa_uint8_t *tosca_type)
{
    qosa_sms_address_info_t address = {0};

    // Parameter check
    if (sca_buf == QOSA_NULL || tosca_type == QOSA_NULL)
    {
        return;
    }

    // Get SMS center address
    if (QOSA_SMS_SUCCESS != qosa_sms_get_sca(g_sms_simid, &address))
    {
        return;
    }

    // Convert to string
    if (QOSA_SMS_SUCCESS != qosa_sms_address_to_text(&address, sca_buf, buf_len))
    {
        return;
    }

    // Return address type
    *tosca_type = address.address_type;

    return;
}

/**
 * @brief Set SMS messages character encoding format
 *
 * @param[in] data_chset
 *          - SMS messages character encoding format
 * @return void
 * @note
 */
void qosa_sms_set_messages_chset(qosa_at_chset_e data_chset)
{
    g_sms_messages_chset = data_chset;

    return;
}

/**
 * @brief Get SMS messages character encoding format
 *
* @return qosa_at_chset_e
 *          - Current SMS messages character encoding format
 * @note
 */
qosa_at_chset_e qosa_sms_get_messages_chset(void)
{
    return g_sms_messages_chset;
}

/**
 * @brief Initialize SMS configuration with default values
 *
 * @return void
 * @note
 */
static void qosa_sms_demo_init_config(void)
{
    int i = 0;
    for (i = 0; i < CONFIG_QOSA_SIM_HAL_SLOT_NUM; i++)
    {
        g_sms_cfg.format[i] = QOSA_SMS_DEMO_FORMAT_TEXT;
        g_sms_cfg.mem_ready[i] = QOSA_FALSE;
        g_sms_cfg.mem[i].mem1 = QOSA_SMS_STOR_ME;
        g_sms_cfg.mem[i].mem2 = QOSA_SMS_STOR_ME;
        g_sms_cfg.mem[i].mem3 = QOSA_SMS_STOR_ME;
    }
    QLOGI("SMS configuration initialized with default values");
}

/**
 * @brief SMS status event handler function
 *
 * @param[in] event
 *          - SMS initialization status event
 * @return void
 * @note
 */
static void qosa_sms_demo_sms_status_handler(qosa_sms_init_status_event_t *event)
{
    g_sms_simid = event->simid;
    qosa_sms_cfg_t sms_conf = {0};

    QLOGI("sim:%d status:%d", event->simid, event->status);

    if (event->status != QOSA_SMS_INIT_STATUS_READY)
    {
        g_sms_cfg.mem_ready[g_sms_simid] = QOSA_FALSE;
        return;
    }

    // Get default storage for various operations from underlying layer
    qosa_sms_get_config(g_sms_simid, &sms_conf);

    g_sms_cfg.mem[g_sms_simid].mem1 = sms_conf.mem1;
    g_sms_cfg.mem[g_sms_simid].mem2 = sms_conf.mem2;
    g_sms_cfg.mem[g_sms_simid].mem3 = sms_conf.mem3;

    // Set storage OK flag
    g_sms_cfg.mem_ready[g_sms_simid] = QOSA_TRUE;
    QLOGI("g_sms_cfg.mem_ready[%d]:%d", g_sms_simid, g_sms_cfg.mem_ready[g_sms_simid]);
}

/**
 * @brief Handle SIM module reported ind events
 *
 * @param[in] cmd
 *          - Command event
 * @param[in] user_data_ptr
 *          - User data pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_sms_indicate_hdlr(qosa_center_cmd_event_enum_e cmd, void *user_data_ptr)
{
    QOSA_UNUSED(cmd);

    qosa_sms_demo_report_t *msg = user_data_ptr;

    if (msg == QOSA_NULL)
    {
        return;
    }

    QLOGI("msg cmd:%d", msg->cmd);
    switch (msg->cmd)
    {
        case QOSA_EVENT_MODEM_SMS_STATUS:
            qosa_sms_demo_sms_status_handler(&msg->init_status_event);
            break;

        default:
            break;
    }

    qosa_free(msg);
    msg = QOSA_NULL;

    return;
}

/**
 * @brief Modem SMS status event callback function
 *
 * @param[in] user_argv
 *          - User parameter
 * @param[in] argv
 *          - Event parameter
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int unirtos_modem_sms_status_event_cb(void *user_argv, void *argv)
{
    QOSA_UNUSED(user_argv);
    size_t                  i = 0;
    qosa_sms_demo_report_t *msg = QOSA_NULL;

    msg = qosa_malloc(sizeof(qosa_sms_demo_report_t));
    if (!msg)
    {
        return -1;
    }

    qosa_memcpy(&msg->init_status_event, argv, sizeof(qosa_sms_init_status_event_t));

    msg->cmd = QOSA_EVENT_MODEM_SMS_STATUS;
    qosa_center_send_cmd(QOSA_CENTER_EVENT_SMS_IND_CMD, msg);

    for (i = 0; i < CONFIG_QOSA_SIM_HAL_SLOT_NUM; i++)
    {
        QLOGI("mem1:%d mem2:%d mem3:%d", g_sms_cfg.mem[i].mem1, g_sms_cfg.mem[i].mem2, g_sms_cfg.mem[i].mem3);
    }

    return 0;
}

/**
 * @brief Initialize SIM card demo event callback function
 *
 * @return void
 * @note This function is used to register SIM card status related event callback handlers,
 * including SIM card status change events and SIM card insertion/removal status events.
 */
static void qosa_sms_demo_event_cb_init(void)
{
    QLOGI("event Initialized.");
    qosa_event_notify_register(QOSA_EVENT_MODEM_SMS_STATUS, unirtos_modem_sms_status_event_cb, QOSA_NULL);
    qosa_center_set_cmd_handler(QOSA_CENTER_EVENT_SMS_IND_CMD, qosa_sms_demo_sms_indicate_hdlr);
}

/**
 * @brief Send SMS response callback
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameter pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_send_msg_rsp(void *ctx, void *argv)
{
    qosa_sms_send_pdu_cnf_t *cnf = argv;

    QLOGI("result sim:%d err:0x%x", g_sms_simid, cnf->err_code);

    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        QLOGI("Send SMS failed with error:%d", cnf->err_code);
    }
    else
    {
        QLOGI("Send SMS success, MR:%u", cnf->mr);
    }
}

/**
 * @brief Initialize SMS list context
 *
 * @param[in] simid
 *          - SIM card ID
 * @param[in] storage
 *          - SMS storage location
 * @param[in] status
 *          - SMS status
 * @return qosa_sms_demo_sms_async_ctx_t*
 *          - Success returns context pointer, failure returns NULL
 * @note
 */
static qosa_sms_demo_sms_async_ctx_t *qosa_sms_demo_list_sms_init(qosa_uint8_t simid, qosa_sms_stor_e storage, qosa_sms_status_e status)
{
    qosa_uint16_t                  list_num = 0;
    qosa_uint16_t                  total_slot = 0;
    qosa_sms_stor_info_t           stor_info = {0};
    qosa_uint16_t                 *list = QOSA_NULL;
    qosa_sms_demo_sms_async_ctx_t *ctx = QOSA_NULL;

    ctx = qosa_malloc(sizeof(qosa_sms_demo_sms_async_ctx_t));
    if (QOSA_NULL == ctx)
    {
        return QOSA_NULL;
    }

    // Read specified storage maximum capacity
    if (QOSA_SMS_SUCCESS != qosa_sms_get_stor_info(simid, &stor_info))
    {
        qosa_free(ctx);
        ctx = QOSA_NULL;

        return QOSA_NULL;
    }
    total_slot = (QOSA_SMS_STOR_ME == storage) ? stor_info.total_me : stor_info.total_sm;
    QLOGI("total_slot:%d", total_slot);

    list = qosa_malloc(sizeof(qosa_uint16_t) * total_slot);
    if (QOSA_NULL == list)
    {
        qosa_free(ctx);
        ctx = QOSA_NULL;

        return QOSA_NULL;
    }

    list_num = total_slot;
    if (QOSA_SMS_SUCCESS != qosa_sms_get_spec_record(simid, storage, status, list, &list_num))
    {
        qosa_free(list);
        qosa_free(ctx);
        list = QOSA_NULL;
        ctx = QOSA_NULL;

        return QOSA_NULL;
    }

    ctx->list = list;
    ctx->list_len = list_num;
    ctx->cur_pos = 0;

    return ctx;
}

/**
 * @brief Get SMS storage information
 *
 * @param[in] simid
 *          - SIM card ID
 * @param[in] mem_info
 *          - Storage information structure pointer
 * @return int
 *          - Success returns QOSA_SMS_SUCCESS, failure returns error code
 * @note
 */
static int qosa_sms_demo_get_mem(qosa_uint8_t simid, qosa_sms_demo_mem_info_t *mem_info)
{
    if (g_sms_cfg.mem_ready[simid] == QOSA_FALSE)
    {
        return QOSA_SMS_NOT_INIT_ERR;
    }

    mem_info->mem1 = g_sms_cfg.mem[simid].mem1;  // read and delete from this memory storage
    mem_info->mem2 = g_sms_cfg.mem[simid].mem2;  // write and send to this memory storage
    mem_info->mem3 = g_sms_cfg.mem[simid].mem3;  // received messages will be placed in this memory storage if routing to PC is not set.

    return QOSA_SMS_SUCCESS;
}

/**
 * @brief Get next record, if obtained returns valid record index, if not returns -1
 *
 * @param[in] ctx
 *          - Asynchronous context
 * @return int
 *          - Returns -1 means no more records, other values mean valid record index
 * @note
 */
static int qosa_sms_demo_list_sms_fetch_next_ex(qosa_sms_demo_sms_async_ctx_t *ctx)
{
    if (ctx->cur_pos >= ctx->list_len)
    {
        QLOGI("no more record");
        return -1;  // All records have been retrieved
    }

    QLOGI("ctx->list[ctx->cur_pos]:%d", ctx->list[ctx->cur_pos]);
    return ctx->list[ctx->cur_pos++];
}

/**
 * @brief Free SMS list context
 *
 * @param[in] ctx
 *          - Context pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_delete_sms_deinit_ex(qosa_sms_demo_sms_async_ctx_t *ctx)
{
    if (ctx)
    {
        qosa_free(ctx->list);
        ctx->list = QOSA_NULL;
        qosa_free(ctx);
        ctx = QOSA_NULL;
    }
}

/**
 * @brief Get SMS status string
 *
 * @param[in] status
 *          - SMS status value
 * @return const char*
 *          - Status string
 * @note
 */
static const char *qosa_sms_demo_sms_msg_status_str(qosa_uint8_t status)
{
    const qosa_value_str_map_t *m = qosa_Vs_map_find_by_value(g_sms_status_vs, status);
    return (m != QOSA_NULL) ? m->str : "";
}

/**
 * @brief List SMS response callback
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameter pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_list_msg_rsp(void *ctx, void *argv)
{
    int                            qosa_err = QOSA_SMS_SUCCESS;
    int                            next_record_index = 0;
    char                           sms_message[1024] = {0};
    char                           prefix[16] = "+SMS LIST";
    qosa_sms_msg_t                 msg = {0};
    qosa_sms_read_param_t          read = {0};
    qosa_sms_demo_format_type_e    format = QOSA_SMS_DEMO_FORMAT_PDU;
    qosa_sms_demo_sms_async_ctx_t *async = ctx;
    qosa_sms_read_pdu_cnf_t       *cnf = argv;
    qosa_sms_record_t             *record = QOSA_NULL;
    qosa_sms_send_t               *send = QOSA_NULL;
    qosa_sms_recv_t               *recv = QOSA_NULL;
    qosa_sms_rept_t               *report = QOSA_NULL;
    qosa_sms_pdu_t                *pdu = QOSA_NULL;

    QLOGI("result sim:%d err:0x%x", g_sms_simid, cnf->err_code);

    format = g_sms_cfg.format[g_sms_simid];

    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        if (QOSA_SMS_NO_MSG_ERR == cnf->err_code)
        {
            qosa_sms_demo_delete_sms_deinit_ex(async);
            return;
        }
        else
        {
            QLOGI("list SMS msg failed");
            qosa_sms_demo_delete_sms_deinit_ex(async);
            return;
        }
    }
    else
    {
        record = &cnf->record;
        pdu = &record->pdu;

        qosa_memset(sms_message, 0, 1024);
        if (QOSA_SMS_DEMO_FORMAT_TEXT == format)
        {
            qosa_sms_pdu_to_text(record, &msg);
            send = &msg.text.send;
            recv = &msg.text.recv;
            report = &msg.text.report;

            switch (msg.msg_type)
            {
                case QOSA_SMS_SUBMIT: {
                    qosa_snprintf(
                        sms_message,
                        sizeof(sms_message),
                        "%s: %u, \"%s\", \"%s\", \"%s\"",
                        prefix,
                        send->index,
                        qosa_sms_demo_sms_msg_status_str(send->status),
                        send->da,
                        (const char *)send->data
                    );

                    QLOGI(" %s", sms_message);
                }
                break;

                case QOSA_SMS_DELIVER: {
                    qosa_snprintf(
                        sms_message,
                        sizeof(sms_message),
                        "%s: %u, \"%s\", \"%s\", \"%u/%02u/%02u,%02u:%02u:%02u\", \"%s\"",
                        prefix,
                        recv->index,
                        qosa_sms_demo_sms_msg_status_str(recv->status),
                        recv->oa,
                        recv->scts.year,
                        recv->scts.month,
                        recv->scts.day,
                        recv->scts.hour,
                        recv->scts.minute,
                        recv->scts.second,
                        (const char *)recv->data
                    );

                    QLOGI(" %s", sms_message);
                }
                break;

                case QOSA_SMS_STATUS_REPORT: {
                    qosa_snprintf(
                        sms_message,
                        sizeof(sms_message),
                        "%s: %u,\"%s\",\"%s\",\"%u/%02u/%02u,%02u:%02u:%02u\"",
                        prefix,
                        report->index,
                        qosa_sms_demo_sms_msg_status_str(report->status),
                        report->ra,
                        report->dt.year,
                        report->dt.month,
                        report->dt.day,
                        report->dt.hour,
                        report->dt.minute,
                        report->dt.second
                    );

                    QLOGI(" %s", sms_message);
                }
                break;

                default:
                    break;
            }
        }
        else
        {
            // PDU format
            qcm_utils_data_to_hex_string(pdu->data, pdu->data_len, sms_message);
            QLOGI(" %u, %u, %s", record->status, pdu->data_len, (const char *)sms_message);
        }
    }

    // Reads the next record. Returns -1 when no more records exist, otherwise returns the index of the valid record.
    next_record_index = qosa_sms_demo_list_sms_fetch_next_ex(async);
    if (next_record_index < 0)
    {
        QLOGI("Record reading completed.");
        qosa_sms_demo_delete_sms_deinit_ex(async);
        return;
    }

    // Read the specified record
    read.stor = async->storage;
    read.index = next_record_index;
    qosa_err = qosa_sms_read_pdu_async(g_sms_simid, &read, qosa_sms_demo_list_msg_rsp, async);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        if (QOSA_SMS_NO_MSG_ERR == qosa_err)
        {
            QLOGI("Record reading completed.");
            qosa_sms_demo_delete_sms_deinit_ex(async);
            return;
        }
        else
        {
            QLOGI("Read next SMS failed");
            qosa_sms_demo_delete_sms_deinit_ex(async);
            return;
        }
    }
    else
    {
        return;
    }
}

/**
 * @brief Read SMS response callback
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameter pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_read_msg_rsp(void *ctx, void *argv)
{
    char                       *sms_message = QOSA_NULL;
    qosa_sms_msg_t              msg = {0};
    qosa_sms_read_pdu_cnf_t    *cnf = argv;
    qosa_sms_record_t          *record = &cnf->record;
    qosa_sms_demo_format_type_e format = QOSA_SMS_DEMO_FORMAT_PDU;
    qosa_sms_send_t            *send = QOSA_NULL;
    qosa_sms_recv_t            *recv = QOSA_NULL;
    qosa_sms_rept_t            *report = QOSA_NULL;
    qosa_sms_pdu_t             *pdu = QOSA_NULL;

    sms_message = qosa_calloc(1, 1024);
    if (sms_message == QOSA_NULL)
    {
        QLOGI("calloc failed");
        return;
    }

    QLOGI("result sim:%d err:0x%x", g_sms_simid, cnf->err_code);

    format = g_sms_cfg.format[g_sms_simid];

    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        if (QOSA_SMS_NO_MSG_ERR == cnf->err_code)
        {
            QLOGI("Read SMS success");
            return;
        }
        else
        {
            QLOGI("Read SMS failed");
            return;
        }
    }
    else
    {
        pdu = &record->pdu;

        qosa_memset(sms_message, 0, 1024);
        if (QOSA_SMS_DEMO_FORMAT_TEXT == format)
        {
            qosa_sms_pdu_to_text(record, &msg);
            send = &msg.text.send;
            recv = &msg.text.recv;
            report = &msg.text.report;

            switch (msg.msg_type)
            {
                case QOSA_SMS_SUBMIT: {
                    qosa_snprintf(
                        sms_message,
                        1024,
                        "\"%s\", \"%s\", \"%s\"",
                        qosa_sms_demo_sms_msg_status_str(send->status),
                        send->da,
                        (const char *)send->data
                    );
                    QLOGI("SMS: %s", sms_message);
                }
                break;

                case QOSA_SMS_DELIVER: {
                    qosa_snprintf(
                        sms_message,
                        1024,
                        "\"%s\", \"%s\", \"%u/%02u/%02u,%02u:%02u:%02u\", \"%s\"",
                        qosa_sms_demo_sms_msg_status_str(recv->status),
                        recv->oa,
                        recv->scts.year,
                        recv->scts.month,
                        recv->scts.day,
                        recv->scts.hour,
                        recv->scts.minute,
                        recv->scts.second,
                        (const char *)recv->data
                    );

                    QLOGI("SMS: %s", sms_message);
                }
                break;

                case QOSA_SMS_STATUS_REPORT: {
                    qosa_snprintf(sms_message, 1024, "\"%s\", \"%s\"", qosa_sms_demo_sms_msg_status_str(report->status), report->ra);
                    QLOGI("SMS: %s", sms_message);
                }
                break;

                default:
                    break;
            }
        }
        else
        {
            // PDU format
            qcm_utils_data_to_hex_string(pdu->data, pdu->data_len, sms_message);
            QLOGI("SMS: %u, %u, %s", record->status, pdu->data_len, (const char *)sms_message);
        }
    }

    if (sms_message)
    {
        qosa_free(sms_message);
        sms_message = QOSA_NULL;
    }

    return;
}

/**
 * @brief Delete SMS response callback
 *
 * @param[in] ctx
 *          - Context pointer
 * @param[in] argv
 *          - Response parameter pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_delete_msg_rsp(void *ctx, void *argv)
{
    qosa_sms_general_cnf_t *cnf = argv;

    QLOGI("Delete SMS result sim:%d err:0x%x", g_sms_simid, cnf->err_code);

    if (QOSA_SMS_SUCCESS != cnf->err_code)
    {
        if (QOSA_SMS_NO_MSG_ERR == cnf->err_code)
        {
            QLOGI("Delete SMS success");
            return;
        }
        else
        {
            QLOGI("Delete SMS failed");
            return;
        }
    }
    else
    {
        QLOGI("Delete SMS success");
        return;
    }
}

#if QOSA_SMS_DEMO_TEST_TASK_ENABLE
// example: qosa_sms_demo_send_all_characters_sms("10086", "你好hello")
static int qosa_sms_demo_send_all_characters_sms(const char *phone_number, const char *message_txt)
{
    int                     qosa_err = QOSA_SMS_SUCCESS;
    qosa_uint8_t            qosa_sms_simid = 0;
    qosa_bool_t             is_attached = 0;
    qosa_uint16_t           message_len = 0;
    qosa_sms_msg_t          message = {0};
    qosa_sms_send_param_t   send_param = {0};
    qosa_sms_cfg_t          sms_conf = {0};
    qosa_uint8_t           *pdu_with_sca = QOSA_NULL;
    qosa_uint8_t            pdu_with_sca_len = 0;
    qosa_sms_record_t       record = {0};
    qosa_size_t             max_hex_size = 0;
    char                   *message_ucs2_string = QOSA_NULL;

    if( (message_txt == QOSA_NULL) || (phone_number == QOSA_NULL) )
    {
        return -1;
    }
    is_attached = qosa_datacall_wait_attached(qosa_sms_simid, QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT);
    if (!is_attached)
    {
        QLOGI("attach fail");
        return -1;
    }
    // required max buffer size for UCS-2 conversion
    max_hex_size = (qosa_strlen(message_txt) * 4) + 1;
    message_ucs2_string = (char*)qosa_malloc(max_hex_size);
    if(!message_ucs2_string)
    {
       QLOGI("malloc fail");
       return -1; 
    }
    // Convert UTF-8 string to UCS-2 encoding
    if(qosa_sms_utf8_to_ucs2(message_txt, message_ucs2_string, max_hex_size) != QOSA_SMS_SUCCESS)
    {
       QLOGI("qosa_sms_utf8_to_ucs2 fail");
       qosa_free(message_ucs2_string);
       return -1;
    }

    message_len = qosa_strlen(message_ucs2_string);
    if (message_len == 0)
    {
        return -1;
    }

    //DA support QOSA_CS_GSM charset only.
    qosa_sms_set_charset(QOSA_CS_GSM);
    // Fill in basic information for text messages.
    message.msg_type = QOSA_SMS_SUBMIT;
    message.text.send.status = 0xff;
    qosa_strcpy(message.text.send.da, (const char *)phone_number);
    message.text.send.toda = 129;
    qosa_strcpy((char *)message.text.send.data, (const char *)message_ucs2_string);
    message.text.send.data_len = message_len;
    // DATA support QOSA_CS_UCS2 charset
    message.text.send.data_chset = QOSA_CS_UCS2;
    
    // Copy the text message configuration information.
    qosa_sms_get_config(g_sms_simid, &sms_conf);
    message.text.send.fo = sms_conf.text_fo;
    message.text.send.pid = sms_conf.text_pid;
    message.text.send.vp = sms_conf.text_vp;

    // If the messages is UCS2 encoding. the PDU is configured as 16bit, the dcs set 0x08
    message.text.send.dcs = 0x08;

    // Copy concatenated SMS information.
    message.is_concatenated = QOSA_FALSE;
    message.concat.msg_ref_number = 0;
    message.concat.msg_seg = 0;
    message.concat.msg_total = 0;
    do {
        // Convert text message to pdu message
        qosa_err = qosa_sms_text_to_pdu(&message, &record);
        if (qosa_err != QOSA_SMS_SUCCESS)
        {
            break;
        }
        pdu_with_sca = record.pdu.data;
        pdu_with_sca_len = record.pdu.data_len; 
    
        send_param.pdu.data_len = pdu_with_sca_len;
        qosa_memcpy(send_param.pdu.data, pdu_with_sca, pdu_with_sca_len);
        qosa_err = qosa_sms_send_pdu_async(qosa_sms_simid, &send_param, qosa_sms_demo_send_msg_rsp, pdu_with_sca);
    } while (0);
    
    qosa_free(message_ucs2_string);
    
    if(QOSA_SMS_SUCCESS != qosa_err)
    {
        QLOGI("SEND SMS FAILED qosa_err:%d",qosa_err);
        return -1;
    }

    return 0;
}
#endif /* QOSA_SMS_DEMO_TEST_TASK_ENABLE */

/**
 * @brief Send SMS function
 *
 * @param[in] phone_number
 *          - Target phone number
 * @param[in] message_txt
 *          - SMS content
 * @param[in] pdu_length
 *          - PDU length
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_send_sms(const char *phone_number, const char *message_txt, qosa_uint32_t pdu_length)
{
    int                         qosa_err = QOSA_SMS_SUCCESS;
    qosa_bool_t                 is_attached = 0;
    qosa_uint16_t               message_len = 0;
    qosa_sms_msg_t              message = {0};
    qosa_sms_send_param_t       send_param = {0};
    qosa_sms_demo_format_type_e format = g_sms_cfg.format[g_sms_simid];

    if ((message_txt == QOSA_NULL) || (phone_number == QOSA_NULL))
    {
        return -1;
    }

    is_attached = qosa_datacall_wait_attached(g_sms_simid, QOSA_SMS_DEMO_WAIT_ATTACH_TIMEOUT);
    if (!is_attached)
    {
        QLOGI("attach fail");
        return -1;
    }

    if (pdu_length > 0)
    {
        format = QOSA_SMS_DEMO_FORMAT_PDU;
    }
    else
    {
        format = QOSA_SMS_DEMO_FORMAT_TEXT;
    }

    message_len = qosa_strlen(message_txt);
    if (message_len == 0)
    {
        return -1;
    }

    // Construct text message data information.
    if (QOSA_SMS_DEMO_FORMAT_TEXT == format)
    {
        qosa_sms_cfg_t sms_conf = {0};

        // Fill in basic information for text messages.
        message.msg_type = QOSA_SMS_SUBMIT;
        message.text.send.status = 0xff;
        qosa_strcpy(message.text.send.da, (const char *)phone_number);
        message.text.send.toda = 129;
        qosa_strcpy((char *)message.text.send.data, (const char *)message_txt);
        message.text.send.data_len = message_len;

        message.text.send.data_chset = qosa_sms_get_messages_chset();
        // Copy the text message configuration information.
        qosa_sms_get_config(g_sms_simid, &sms_conf);
        message.text.send.fo = sms_conf.text_fo;
        message.text.send.pid = sms_conf.text_pid;
        message.text.send.vp = sms_conf.text_vp;

        // If the messages is UCS2 encoding. the PDU is configured as 16bit, the dcs set 0x08
        message.text.send.dcs = (message.text.send.data_chset == QOSA_CS_UCS2) ? 0x08 : sms_conf.text_dcs;

        // Copy concatenated SMS information.
        message.is_concatenated = QOSA_FALSE;
        message.concat.msg_ref_number = 0;
        message.concat.msg_seg = 0;
        message.concat.msg_total = 0;
    }

    do {
        // convert text message to pdu message
        qosa_uint8_t     *pdu_with_sca = QOSA_NULL;  // Free memory in PDU mode only, TEXT mode excludes freeing.
        qosa_uint8_t      pdu_with_sca_len = 0;
        qosa_sms_record_t record = {0};
        qosa_uint8_t      sca_len = 0;

        // Construct the PDU data to be written/sent.
        if (QOSA_SMS_DEMO_FORMAT_TEXT == format)
        {
            // In TEXT mode, convert to PDU format first.
            qosa_err = qosa_sms_text_to_pdu(&message, &record);
            if (qosa_err != QOSA_SMS_SUCCESS)
            {
                break;
            }
            pdu_with_sca = record.pdu.data;
            pdu_with_sca_len = record.pdu.data_len;
        }
        else
        {
            // In PDU mode, convert the hex string to a byte stream.
            pdu_with_sca = qosa_malloc(message_len);
            if ((pdu_with_sca == QOSA_NULL) || (QOSA_OK != qcm_utils_data_string_to_hex((const char *)message_txt, message_len, pdu_with_sca)))
            {
                qosa_err = QOSA_SMS_CMS_INVALID_PARA;
                qosa_free(pdu_with_sca);
                pdu_with_sca = QOSA_NULL;
                break;
            }

            sca_len = QOSA_SMS_GET_SCA_PART_LEN(pdu_with_sca);
            if ((sca_len + pdu_length) != message_len / 2)
            {
                qosa_err = QOSA_SMS_CMS_INVALID_PARA;
                qosa_free(pdu_with_sca);
                pdu_with_sca = QOSA_NULL;
                break;
            }
            pdu_with_sca_len = message_len / 2;
        }

        send_param.pdu.data_len = pdu_with_sca_len;
        qosa_memcpy(send_param.pdu.data, pdu_with_sca, pdu_with_sca_len);
        qosa_err = qosa_sms_send_pdu_async(g_sms_simid, &send_param, qosa_sms_demo_send_msg_rsp, pdu_with_sca);

        // Free memory in PDU mode.
        if (QOSA_SMS_DEMO_FORMAT_PDU == format)
        {
            qosa_free(pdu_with_sca);
            pdu_with_sca = QOSA_NULL;
        }

    } while (0);

    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        QLOGI("SEND SMS FAILED qosa_err:%d", qosa_err);
        return -1;
    }

    return 0;
}

/**
 * @brief List SMS function
 *
 * @param[in] stat
 *          - SMS status filter condition
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_list_sms(const char *stat)
{
    int                            qosa_err = 0;
    int                            next_record_index = 0;
    qosa_sms_demo_format_type_e    format = g_sms_cfg.format[g_sms_simid];
    qosa_sms_stor_e                storage = QOSA_SMS_STOR_ME;
    qosa_sms_status_e              status = QOSA_SMS_ALL;
    qosa_sms_demo_mem_info_t       mem_info = {0};
    qosa_sms_demo_sms_async_ctx_t *async = QOSA_NULL;
    const qosa_value_str_map_t    *str_map = QOSA_NULL;

    str_map = (qosa_value_str_map_t *)qosa_Vs_map_find_by_istr((qosa_value_str_map_t *)g_sms_status_vs, stat);
    if (str_map == QOSA_NULL)
    {
        QLOGI("param failed");
        return -1;
    }
    status = str_map->value;

    qosa_err = qosa_sms_demo_get_mem(g_sms_simid, &mem_info);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        QLOGI("get memory storage failed");
        return -1;
    }

    storage = mem_info.mem1;

    // Obtain the next index for reading
    async = qosa_sms_demo_list_sms_init(g_sms_simid, storage, status);
    if (async == QOSA_NULL)
    {
        QLOGI("SMS list index init failed");
        return -1;
    }

    async->format = format;
    async->storage = storage;

    // Get first record index
    next_record_index = qosa_sms_demo_list_sms_fetch_next_ex(async);
    if (next_record_index < 0)
    {
        qosa_sms_demo_delete_sms_deinit_ex(async);
        return 0;
    }

    qosa_sms_read_param_t read = {0};
    read.stor = storage;
    read.index = next_record_index;

    qosa_err = qosa_sms_read_pdu_async(g_sms_simid, &read, qosa_sms_demo_list_msg_rsp, async);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        if (QOSA_SMS_NO_MSG_ERR == qosa_err)
        {
            QLOGI("SMS read success");
            qosa_sms_demo_delete_sms_deinit_ex(async);
            return 0;
        }
        else
        {
            QLOGI("SMS read failed");
            qosa_sms_demo_delete_sms_deinit_ex(async);
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Read SMS function
 *
 * @param[in] index
 *          - SMS index
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_read_sms(qosa_uint16_t index)
{
    int                      qosa_err = QOSA_SMS_SUCCESS;
    qosa_sms_stor_e          storage = QOSA_SMS_STOR_ME;
    qosa_uint16_t            total_slot = 0;
    qosa_sms_stor_info_t     stor_info = {0};
    qosa_sms_demo_mem_info_t mem_info = {0};
    qosa_sms_read_param_t    read = {0};

    if (index < 0)
    {
        QLOGI("index error");
        return -1;
    }

    qosa_err = qosa_sms_demo_get_mem(g_sms_simid, &mem_info);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        QLOGI("get memory storage failed");
        return -1;
    }

    storage = mem_info.mem1;

    if (QOSA_SMS_SUCCESS != qosa_sms_get_stor_info(g_sms_simid, &stor_info))
    {
        QLOGI("get memory storage failed");
        return -1;
    }

    // read method use mem1
    total_slot = (QOSA_SMS_STOR_ME == storage) ? stor_info.total_me : stor_info.total_sm;

    if (index > total_slot - 1)
    {
        QLOGI("no index SMS");
        return -1;
    }

    read.stor = storage;
    read.index = index;
    qosa_err = qosa_sms_read_pdu_async(g_sms_simid, &read, qosa_sms_demo_read_msg_rsp, QOSA_NULL);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        if (QOSA_SMS_NO_MSG_ERR == qosa_err)
        {
            QLOGI("SMS read success");
            return 0;
        }
        else
        {
            QLOGI("SMS read failed");
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Delete SMS function
 *
 * @param[in] index
 *          - SMS index
 * @param[in] delflag
 *          - Delete flag
 * @return int
 *          - Success returns 0, failure returns -1
 * @note
 */
static int qosa_sms_demo_delete_sms(qosa_uint16_t index, qosa_sms_delflag_e delflag)
{
    int                      qosa_err = 0;
    qosa_uint16_t            total_slot = 0;
    qosa_sms_stor_e          storage = QOSA_SMS_STOR_ME;
    qosa_sms_demo_mem_info_t mem_info = {0};
    qosa_sms_stor_info_t     stor_info = {0};
    qosa_sms_delete_param_t delete = {0};

    if (index < 0)
    {
        QLOGI("index failed");
        return -1;
    }

    qosa_err = qosa_sms_demo_get_mem(g_sms_simid, &mem_info);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        QLOGI("get memory storage failed");
        return -1;
    }

    storage = mem_info.mem1;

    // get storage iformation
    if (QOSA_SMS_SUCCESS != qosa_sms_get_stor_info(g_sms_simid, &stor_info))
    {
        QLOGI("get memory storage failed");
        return -1;
    }

    // delete method use mem1
    total_slot = (QOSA_SMS_STOR_ME == storage) ? stor_info.total_me : stor_info.total_sm;
    if (index > total_slot - 1)
    {
        QLOGI("no index SMS");
        return -1;
    }

    delete.index_or_stat = delflag == 0 ? QOSA_SMS_SELECT_BY_INDEX : QOSA_SMS_SELECT_BY_STATUS;
    delete.stor = storage;
    delete.index = index;
    delete.delflag = delflag;

    qosa_err = qosa_sms_delete_async(g_sms_simid, &delete, qosa_sms_demo_delete_msg_rsp, QOSA_NULL);
    if (QOSA_SMS_SUCCESS != qosa_err)
    {
        if (QOSA_SMS_NO_MSG_ERR == qosa_err)
        {
            QLOGI("SMS delete success");
            return 0;
        }
        else
        {
            QLOGI("SMS delete failed");
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Handle send SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_send_sms(qosa_sms_demo_msg_t *msg)
{
    qosa_sms_demo_send_msg_t *sms_data = (qosa_sms_demo_send_msg_t *)msg->data;

    if (sms_data == QOSA_NULL)
    {
        QLOGI("SMS data is NULL");
        return;
    }

    // Send SMS.
    if (qosa_sms_demo_send_sms(sms_data->phone_number, sms_data->message, sms_data->pdu_length) == 0)
    {
        QLOGI("send SMS success");
    }
    else
    {
        QLOGI("send SMS failed");
    }

    // Release SMS data memory.
    qosa_free(sms_data);
    sms_data = QOSA_NULL;

    return;
}

/**
 * @brief Handle read SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_read_sms(qosa_sms_demo_msg_t *msg)
{
    qosa_uint16_t *index = (qosa_uint16_t *)msg->data;

    if (index == QOSA_NULL)
    {
        QLOGI("index is NULL");
        return;
    }

    if (qosa_sms_demo_read_sms(*index) != 0)
    {
        QLOGI("read SMS failed");
    }

    // Release inddex memory.
    qosa_free(index);
    index = QOSA_NULL;

    return;
}

/**
 * @brief Handle delete SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_delete_sms(qosa_sms_demo_msg_t *msg)
{
    qosa_sms_demo_delete_msg_t *sms_delete_data = (qosa_sms_demo_delete_msg_t *)msg->data;

    if (sms_delete_data == QOSA_NULL)
    {
        QLOGI("delete SMS failed");
        return;
    }

    if (qosa_sms_demo_delete_sms(sms_delete_data->index, sms_delete_data->delflag) != 0)
    {
        QLOGI("delete SMS failed");
    }

    // Release SMS delete data memory.
    qosa_free(sms_delete_data);
    sms_delete_data = QOSA_NULL;

    return;
}

/**
 * @brief Handle list SMS request
 *
 * @param[in] msg
 *          - Message pointer
 * @return void
 * @note
 */
static void qosa_sms_demo_handle_list_sms(qosa_sms_demo_msg_t *msg)
{
    char *stat = (char *)msg->data;

    if (qosa_sms_demo_list_sms(stat) != 0)
    {
        QLOGI("list SMS failed");
    }

    if (stat != QOSA_NULL)
    {
        qosa_free(stat);
        stat = QOSA_NULL;
    }

    return;
}

/**
 * @brief Send Short Message Service (SMS) API
 *
 * @param[in] phone_number
 *          - Destination phone number string
 * @param[in] message
 *          - SMS message content
 * @param[in] pdu_length
 *          - Protocol Data Unit length (set to 0 for text format SMS)
 * @return int
 *          - Success returns 0, failure returns -1
 * @note For text SMS, set pdu_length to 0; for PDU format, provide actual PDU length
 */
int qosa_sms_send_message(const char *phone_number, const char *message, const qosa_uint32_t pdu_length)
{
    int                       ret = 0;
    qosa_sms_demo_send_msg_t *sms_data = QOSA_NULL;

    if (phone_number == QOSA_NULL || message == QOSA_NULL)
    {
        QLOGI("send message param error");
        return -1;
    }

    if (qosa_strlen(message) > QOSA_SMS_DEMO_SMS_MAX_LENGTH)
    {
        QLOGI("SMS message is too long");
        return -1;
    }

    sms_data = (qosa_sms_demo_send_msg_t *)qosa_malloc(sizeof(qosa_sms_demo_send_msg_t));
    if (sms_data == QOSA_NULL)
    {
        QLOGI("SMS data malloc failed");
        return -1;
    }

    // Copy message.
    qosa_strncpy(sms_data->phone_number, phone_number, sizeof(sms_data->phone_number) - 1);
    qosa_strncpy(sms_data->message, message, sizeof(sms_data->message) - 1);
    sms_data->pdu_length = pdu_length;

    // Construct message.
    qosa_sms_demo_msg_t msg = {.msg_type = SMS_DEMO_MSG_SEND_SMS, .data = sms_data, .data_len = sizeof(qosa_sms_demo_send_msg_t)};

    // Send message.
    ret = qosa_msgq_release(g_sms_demo_msgq, sizeof(qosa_sms_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
    if (ret != QOSA_OK)
    {
        qosa_free(sms_data);
        sms_data = QOSA_NULL;

        return -1;
    }

    return 0;
}

/**
 * @brief Read Short Message Service (SMS) API
 *
 * @param[in] index
 *          - Index of the SMS message to read
 * @return int
 *          - Success returns 0, failure returns -1
 * @note The index refers to the position of the SMS in the storage
 */
int qosa_sms_read_message(qosa_uint16_t index)
{
    int            ret = 0;
    qosa_uint16_t *index_ptr = QOSA_NULL;

    index_ptr = (qosa_uint16_t *)qosa_malloc(sizeof(qosa_uint16_t));
    if (index_ptr == QOSA_NULL)
    {
        QLOGI("malloc failed");
        return -1;
    }

    *index_ptr = index;

    // Construct message.
    qosa_sms_demo_msg_t msg = {.msg_type = SMS_DEMO_MSG_READ_SMS, .data = index_ptr, .data_len = sizeof(qosa_uint16_t)};

    // Send message.
    ret = qosa_msgq_release(g_sms_demo_msgq, sizeof(qosa_sms_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
    if (ret != QOSA_OK)
    {
        qosa_free(index_ptr);
        index_ptr = QOSA_NULL;

        return -1;
    }

    return 0;
}

/**
 * @brief Delete Short Message Service (SMS) API
 *
 * @param[in] index
 *          - Index of the SMS message to delete
 * @param[in] delflag
 *          - Delete flag
 * @return int
 *          - Success returns 0, failure returns -1
 * @note The index specifies which SMS message to remove from storage
 */
int qosa_sms_delete_message(qosa_uint16_t index, qosa_sms_delflag_e delflag)
{
    int                         ret = 0;
    qosa_sms_demo_delete_msg_t *sms_delete_data = QOSA_NULL;

    if (!((QOSA_SMS_DEL_INDEX <= delflag) && (delflag <= QOSA_SMS_DEL_ALL)))
    {
        QLOGI("param error");
        return -1;
    }

    sms_delete_data = (qosa_sms_demo_delete_msg_t *)qosa_malloc(sizeof(qosa_sms_demo_delete_msg_t));
    if (sms_delete_data == QOSA_NULL)
    {
        return -1;
    }

    sms_delete_data->index = index;
    sms_delete_data->delflag = delflag;

    // Construct message.
    qosa_sms_demo_msg_t msg = {.msg_type = SMS_DEMO_MSG_DELETE_SMS, .data = sms_delete_data, .data_len = sizeof(int)};

    // Send message.
    ret = qosa_msgq_release(g_sms_demo_msgq, sizeof(qosa_sms_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
    if (ret != QOSA_OK)
    {
        qosa_free(sms_delete_data);
        sms_delete_data = QOSA_NULL;

        return -1;
    }

    return 0;
}

/**
 * @brief List Short Message Service (SMS) API
 *
 * @param[in] stat
 *          - SMS status filter condition (e.g., "ALL", "REC UNREAD", "REC READ")
 * @return int
 *          - Success returns 0, failure returns -1
 * @note Filter messages by status: ALL, REC UNREAD, REC READ, STO UNSENT, STO SENT
 */
int qosa_sms_list_messages(const char *stat)
{
    int   ret = 0;
    char *stat_copy = QOSA_NULL;

    if (stat != QOSA_NULL)
    {
        stat_copy = (char *)qosa_malloc(qosa_strlen(stat) + 1);
        if (stat_copy == QOSA_NULL)
        {
            return -1;
        }
        qosa_strcpy(stat_copy, stat);
    }

    // Construct message.
    qosa_sms_demo_msg_t msg = {.msg_type = SMS_DEMO_MSG_LIST_SMS, .data = stat_copy, .data_len = stat_copy ? qosa_strlen(stat_copy) + 1 : 0};

    // Send message.
    ret = qosa_msgq_release(g_sms_demo_msgq, sizeof(qosa_sms_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
    if (ret != QOSA_OK)
    {
        if (stat_copy != QOSA_NULL)
        {
            qosa_free(stat_copy);
            stat_copy = QOSA_NULL;
        }

        return -1;
    }

    return 0;
}

/**
 * @brief Stop SMS demonstration program
 *
 * @return void
 * @note This function terminates the SMS demo execution and releases related resources
 */
void qosa_sms_stop(void)
{
    if (g_sms_demo_msgq != QOSA_NULL)
    {
        qosa_sms_demo_msg_t msg = {.msg_type = SMS_DEMO_MSG_EXIT, .data = QOSA_NULL, .data_len = 0};

        qosa_msgq_release(g_sms_demo_msgq, sizeof(qosa_sms_demo_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
    }
}

#if QOSA_SMS_DEMO_TEST_TASK_ENABLE
/**
 * @brief SMS demonstration test function
 *
 * @return void
 * @note This function performs comprehensive testing of SMS functionality including sending, reading, listing and deleting messages
 */
void qosa_sms_demo_test_example(void)
{
    // Wait for a period of time to allow system initialization.
    qosa_task_sleep_sec(20);

    // Send text SMS.
    // qosa_sms_set_center_number("+8613010591500", 145);
    qosa_task_sleep_sec(30);
    qosa_sms_set_charset(QOSA_CS_GSM);
    qosa_sms_set_messages_chset(QOSA_CS_UCS2);
    qosa_sms_send_message("10086", "0053004D0053002000740065007300740021", 0);

    //Send Chinese SMS
    qosa_sms_demo_send_all_characters_sms("10086", "你好hello");

    qosa_task_sleep_sec(30);
    qosa_sms_set_charset(QOSA_CS_GSM);
    qosa_sms_set_messages_chset(QOSA_CS_GSM);
    qosa_sms_send_message("10086", "SMS test!", 0);

    // List unread SMS messages.
    qosa_task_sleep_sec(20);
    qosa_sms_list_messages("REC UNREAD");

    // List all SMS messages.
    qosa_task_sleep_sec(60);
    qosa_sms_list_messages("ALL");

    // Read SMS messages (e.g., index = 0).
    qosa_task_sleep_sec(30);
    qosa_sms_read_message(0);

    // Delete SMS messages (e.g., index = 0).
    qosa_task_sleep_sec(30);
    qosa_sms_delete_message(0, QOSA_SMS_DEL_INDEX);

    // Send PDU SMS.
    // qosa_sms_set_center_number("+8613800771500", 145);
    qosa_task_sleep_sec(30);
    qosa_sms_send_message("10086", "0891683108701705F0110005910180F60000FF05E8329BFD06", 16);

    // List all SMS messages.
    qosa_task_sleep_sec(30);
    qosa_sms_list_messages("ALL");

    // Exit SMS Demo.
    qosa_task_sleep_sec(30);
    qosa_sms_stop();

    return;
}

/**
 * @brief SMS demonstration test task
 *
 * @param[in] arg
 *          - Task parameter pointer (unused)
 * @return void
 * @note
 */
void qosa_sms_demo_test_task(void *arg)
{
    QOSA_UNUSED(arg);

    qosa_sms_demo_test_example();

    if (g_sms_demo_test_task_ptr != QOSA_NULL)
    {
        g_sms_demo_test_task_ptr = QOSA_NULL;
    }

    return;
}
#endif /* QOSA_SMS_DEMO_TEST_TASK_ENABLE */

/**
 * @brief Main function for SMS demonstration task
 *
 * @param[in] arg
 *          - Task parameter pointer (unused)
 * @return void
 * @note Initializes event callbacks and enters main demo loop
 */
void qosa_sms_demo_task(void *arg)
{
    QOSA_UNUSED(arg);
    int                 ret = 0;
    qosa_sms_demo_msg_t msg = {0};

    qosa_task_sleep_sec(10);

    // Create message queue.
    ret = qosa_msgq_create(&g_sms_demo_msgq, sizeof(qosa_sms_demo_msg_t), 20);
    if (ret != QOSA_OK)
    {
        goto exit;
    }

    // Primary message handling cycle.
    while (1)
    {
        ret = qosa_msgq_wait(g_sms_demo_msgq, (qosa_uint8_t *)&msg, sizeof(qosa_sms_demo_msg_t), QOSA_WAIT_FOREVER);
        if (ret != QOSA_OK)
        {
            continue;
        }

        QLOGI("msg.msg_type:%d", msg.msg_type);
        switch (msg.msg_type)
        {
            case SMS_DEMO_MSG_SEND_SMS:
                qosa_sms_demo_handle_send_sms(&msg);
                break;

            case SMS_DEMO_MSG_READ_SMS:
                qosa_sms_demo_handle_read_sms(&msg);
                break;

            case SMS_DEMO_MSG_DELETE_SMS:
                qosa_sms_demo_handle_delete_sms(&msg);
                break;

            case SMS_DEMO_MSG_LIST_SMS:
                qosa_sms_demo_handle_list_sms(&msg);
                break;

            case SMS_DEMO_MSG_EXIT:
                // Received exit message, preparing to exit ...
                goto exit;

            default:
                // Invalid message type
                break;
        }
    }

exit:

    QLOGI("=== SMS Demo Finished ===");

    if (g_sms_demo_msgq != QOSA_NULL)
    {
        qosa_msgq_delete(g_sms_demo_msgq);
        g_sms_demo_msgq = QOSA_NULL;
    }

    if (g_sms_demo_task_ptr != QOSA_NULL)
    {
        g_sms_demo_task_ptr = QOSA_NULL;
    }

    return;
}

/**
 * @brief Initialize SMS demonstration program
 *
 * @return void
 * @note This function sets up SMS configuration, event callbacks, and creates necessary tasks for the demo
 */
void unir_sms_demo_init(void)
{
    int err = 0;

    // SMS configuration initialization.
    qosa_sms_demo_init_config();
    // Register event callback routines.
    qosa_sms_demo_event_cb_init();

    QLOGI("enter UniRTOS SMS DEMO !!!");
    if (g_sms_demo_task_ptr == QOSA_NULL)
    {
        err = qosa_task_create(&g_sms_demo_task_ptr, 4 * 1024, QOSA_PRIORITY_NORMAL, "sms_demo", qosa_sms_demo_task, QOSA_NULL);
        if (err != QOSA_OK)
        {
            QLOGI("SMS demo init task create error");
            return;
        }
    }

#if QOSA_SMS_DEMO_TEST_TASK_ENABLE
    err = qosa_task_create(&g_sms_demo_test_task_ptr, 4 * 1024, QOSA_PRIORITY_NORMAL, "sms_test_demo", qosa_sms_demo_test_task, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGI("SMS demo init test task create error");
        return;
    }
#endif /* QOSA_SMS_DEMO_TEST_TASK_ENABLE */

    return;
}
UNIRTOS_APP_EXPORT(331, "sms_demo", unir_sms_demo_init);