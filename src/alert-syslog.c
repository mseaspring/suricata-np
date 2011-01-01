/* Copyright (C) 2007-2010 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Gurvinder Singh <gurvindersinghdahiya@gmail.com>
 *
 * Logs alerts in a line based text format in to syslog.
 *
 */

#include "suricata-common.h"
#include "debug.h"
#include "flow.h"
#include "conf.h"

#include "threads.h"
#include "tm-threads.h"
#include "threadvars.h"
#include "tm-modules.h"

#include "detect.h"
#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-reference.h"

#include "output.h"
#include "alert-syslog.h"

#include "util-classification-config.h"
#include "util-debug.h"
#include "util-print.h"
#include "util-proto-name.h"
#include "util-syslog.h"

#define DEFAULT_ALERT_SYSLOG_FACILITY_STR       "local0"
#define DEFAULT_ALERT_SYSLOG_FACILITY           LOG_LOCAL0
#define DEFAULT_ALERT_SYSLOG_LEVEL              LOG_ERR
#define MODULE_NAME                             "AlertSyslog"

typedef struct AlertSyslogThread_ {
    /** LogFileCtx has the pointer to the file and a mutex to allow multithreading */
    LogFileCtx* file_ctx;
} AlertSyslogThread;

TmEcode AlertSyslog (ThreadVars *, Packet *, void *, PacketQueue *, PacketQueue *);
TmEcode AlertSyslogIPv4(ThreadVars *, Packet *, void *, PacketQueue *, PacketQueue *);
TmEcode AlertSyslogIPv6(ThreadVars *, Packet *, void *, PacketQueue *, PacketQueue *);
TmEcode AlertSyslogThreadInit(ThreadVars *, void *, void **);
TmEcode AlertSyslogThreadDeinit(ThreadVars *, void *);
void AlertSyslogExitPrintStats(ThreadVars *, void *);
void AlertSyslogRegisterTests(void);
OutputCtx *AlertSyslogInitCtx(ConfNode *);
static void AlertSyslogDeInitCtx(OutputCtx *);

/** \brief   Function to register the AlertSyslog module */
void TmModuleAlertSyslogRegister (void) {
    tmm_modules[TMM_ALERTSYSLOG].name = MODULE_NAME;
    tmm_modules[TMM_ALERTSYSLOG].ThreadInit = AlertSyslogThreadInit;
    tmm_modules[TMM_ALERTSYSLOG].Func = AlertSyslog;
    tmm_modules[TMM_ALERTSYSLOG].ThreadExitPrintStats = AlertSyslogExitPrintStats;
    tmm_modules[TMM_ALERTSYSLOG].ThreadDeinit = AlertSyslogThreadDeinit;
    tmm_modules[TMM_ALERTSYSLOG].RegisterTests = NULL;
    tmm_modules[TMM_ALERTSYSLOG].cap_flags = 0;

    OutputRegisterModule(MODULE_NAME, "syslog", AlertSyslogInitCtx);
}

/** \brief   Function to register the AlertSyslog module for IPv4 */
void TmModuleAlertSyslogIPv4Register (void) {
    tmm_modules[TMM_ALERTSYSLOG4].name = "AlertSyslogIPv4";
    tmm_modules[TMM_ALERTSYSLOG4].ThreadInit = AlertSyslogThreadInit;
    tmm_modules[TMM_ALERTSYSLOG4].Func = AlertSyslogIPv4;
    tmm_modules[TMM_ALERTSYSLOG4].ThreadExitPrintStats = AlertSyslogExitPrintStats;
    tmm_modules[TMM_ALERTSYSLOG4].ThreadDeinit = AlertSyslogThreadDeinit;
    tmm_modules[TMM_ALERTSYSLOG4].RegisterTests = NULL;
}

/** \brief   Function to register the AlertSyslog module for IPv6 */
void TmModuleAlertSyslogIPv6Register (void) {
    tmm_modules[TMM_ALERTSYSLOG6].name = "AlertSyslogIPv6";
    tmm_modules[TMM_ALERTSYSLOG6].ThreadInit = AlertSyslogThreadInit;
    tmm_modules[TMM_ALERTSYSLOG6].Func = AlertSyslogIPv6;
    tmm_modules[TMM_ALERTSYSLOG6].ThreadExitPrintStats = AlertSyslogExitPrintStats;
    tmm_modules[TMM_ALERTSYSLOG6].ThreadDeinit = AlertSyslogThreadDeinit;
    tmm_modules[TMM_ALERTSYSLOG6].RegisterTests = NULL;
}

/**
 * \brief Create a new LogFileCtx for "syslog" output style.
 *
 * \param conf The configuration node for this output.
 * \return A OutputCtx pointer on success, NULL on failure.
 */
OutputCtx *AlertSyslogInitCtx(ConfNode *conf)
{
    const char *enabled = ConfNodeLookupChildValue(conf, "enabled");
    if (enabled != NULL && strncmp(enabled, "no", 2) == 0) {
        SCLogDebug("alert-syslog module has been disabled");
        return NULL;
    }

    const char *facility_s = ConfNodeLookupChildValue(conf, "facility");
    if (facility_s == NULL) {
        facility_s = DEFAULT_ALERT_SYSLOG_FACILITY_STR;
    }

    LogFileCtx *logfile_ctx = LogFileNewCtx();
    if (logfile_ctx == NULL) {
        SCLogDebug("AlertSyslogInitCtx: Could not create new LogFileCtx");
        return NULL;
    }

    int facility = SCMapEnumNameToValue(facility_s, SCGetFacilityMap());
    if (facility == -1) {
        SCLogWarning(SC_ERR_INVALID_ARGUMENT, "Invalid syslog facility: \"%s\","
                " now using \"%s\" as syslog facility", facility_s,
                DEFAULT_ALERT_SYSLOG_FACILITY_STR);
        facility = DEFAULT_ALERT_SYSLOG_FACILITY;
    }

    openlog(NULL, LOG_NDELAY, facility);

    OutputCtx *output_ctx = SCCalloc(1, sizeof(OutputCtx));
    if (output_ctx == NULL) {
        SCLogDebug("AlertSyslogInitCtx: Could not create new OutputCtx");
        return NULL;
    }

    output_ctx->data = logfile_ctx;
    output_ctx->DeInit = AlertSyslogDeInitCtx;

    SCLogInfo("Syslog output initialized");

    return output_ctx;
}

/**
 * \brief Function to clear the memory of the output context and closes the
 *        syslog interface
 *
 * \param output_ctx pointer to the output context to be cleared
 */
static void AlertSyslogDeInitCtx(OutputCtx *output_ctx)
{
    if (output_ctx != NULL) {
        LogFileCtx *logfile_ctx = (LogFileCtx *)output_ctx->data;
        LogFileFreeCtx(logfile_ctx);
        free(output_ctx);
    }
    closelog();
}

/**
 * \brief Function to initialize the AlertSystlogThread and sets the output
 *        context pointer
 *
 * \param tv            Pointer to the threadvars
 * \param initdata      Pointer to the output context
 * \param data          pointer to pointer to point to the AlertSyslogThread
 */
TmEcode AlertSyslogThreadInit(ThreadVars *t, void *initdata, void **data)
{
    AlertSyslogThread *ast = SCMalloc(sizeof(AlertSyslogThread));
    if (ast == NULL)
        return TM_ECODE_FAILED;

    memset(ast, 0, sizeof(AlertSyslogThread));
    if(initdata == NULL)
    {
        SCLogDebug("Error getting context for AlertSyslog. \"initdata\" "
                "argument NULL");
        SCFree(ast);
        return TM_ECODE_FAILED;
    }
    /** Use the Ouptut Context (file pointer and mutex) */
    ast->file_ctx = ((OutputCtx *)initdata)->data;

    *data = (void *)ast;
    return TM_ECODE_OK;
}

/**
 * \brief Function to deinitialize the AlertSystlogThread
 *
 * \param tv            Pointer to the threadvars
 * \param data          pointer to the AlertSyslogThread to be cleared
 */
TmEcode AlertSyslogThreadDeinit(ThreadVars *t, void *data)
{
    AlertSyslogThread *ast = (AlertSyslogThread *)data;
    if (ast == NULL) {
        return TM_ECODE_OK;
    }

    /* clear memory */
    memset(ast, 0, sizeof(AlertSyslogThread));

    SCFree(ast);
    return TM_ECODE_OK;
}

/**
 * \brief   Function which is called to print the IPv4 alerts to the syslog
 *
 * \param tv    Pointer to the threadvars
 * \param p     Pointer to the packet
 * \param data  pointer to the AlertSyslogThread
 * \param pq    pointer the to packet queue
 * \param postpq pointer to the post processed packet queue
 *
 * \return On succes return TM_ECODE_OK
 */
TmEcode AlertSyslogIPv4(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                        PacketQueue *postpq)
{
    AlertSyslogThread *ast = (AlertSyslogThread *)data;
    int i;

    if (p->alerts.cnt == 0)
        return TM_ECODE_OK;

    SCMutexLock(&ast->file_ctx->fp_mutex);

    ast->file_ctx->alerts += p->alerts.cnt;

    for (i = 0; i < p->alerts.cnt; i++) {
        PacketAlert *pa = &p->alerts.alerts[i];

        char srcip[16], dstip[16];

        inet_ntop(AF_INET, (const void *)GET_IPV4_SRC_ADDR_PTR(p), srcip, sizeof(srcip));
        inet_ntop(AF_INET, (const void *)GET_IPV4_DST_ADDR_PTR(p), dstip, sizeof(dstip));

        if (SCProtoNameValid(IPV4_GET_IPPROTO(p)) == TRUE) {
            syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "[%" PRIu32 ":%" PRIu32 ":%"
                    PRIu32 "] %s [Classification: %s] [Priority: %"PRIu32"]"
                    " {%s} %s:%" PRIu32 " -> %s:%" PRIu32 "", pa->gid, pa->sid,
                    pa->rev, pa->msg, pa->class_msg, pa->prio,
                    known_proto[IPV4_GET_IPPROTO(p)], srcip, p->sp, dstip, p->dp);
        } else {
            syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "[%" PRIu32 ":%" PRIu32 ":%"
                    PRIu32 "] %s [Classification: %s] [Priority: %"PRIu32"]"
                    " {PROTO:%03" PRIu32 "} %s:%" PRIu32 " -> %s:%" PRIu32 "",
                    pa->gid, pa->sid, pa->rev, pa->msg, pa->class_msg, pa->prio,
                    IPV4_GET_IPPROTO(p), srcip, p->sp, dstip, p->dp);
        }
    }
    SCMutexUnlock(&ast->file_ctx->fp_mutex);

    return TM_ECODE_OK;
}

/**
 * \brief   Function which is called to print the IPv6 alerts to the syslog
 *
 * \param tv    Pointer to the threadvars
 * \param p     Pointer to the packet
 * \param data  pointer to the AlertSyslogThread
 * \param pq    pointer the to packet queue
 * \param postpq pointer to the post processed packet queue
 *
 * \return On succes return TM_ECODE_OK
 */
TmEcode AlertSyslogIPv6(ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                        PacketQueue *postpq)
{
    AlertSyslogThread *ast = (AlertSyslogThread *)data;
    int i;

    if (p->alerts.cnt == 0)
        return TM_ECODE_OK;

    SCMutexLock(&ast->file_ctx->fp_mutex);

    ast->file_ctx->alerts += p->alerts.cnt;

    for (i = 0; i < p->alerts.cnt; i++) {
        PacketAlert *pa = &p->alerts.alerts[i];
        char srcip[46], dstip[46];

        inet_ntop(AF_INET6, (const void *)GET_IPV6_SRC_ADDR(p), srcip, sizeof(srcip));
        inet_ntop(AF_INET6, (const void *)GET_IPV6_DST_ADDR(p), dstip, sizeof(dstip));

        if (SCProtoNameValid(IPV6_GET_L4PROTO(p)) == TRUE) {
            syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "[%" PRIu32 ":%" PRIu32 ":%"
                    "" PRIu32 "] %s [Classification: %s] [Priority: %"
                    "" PRIu32 "] {%s} %s:%" PRIu32 " -> %s:%" PRIu32 "",
                    pa->gid, pa->sid, pa->rev, pa->msg, pa->class_msg,
                    pa->prio, known_proto[IPV6_GET_L4PROTO(p)], srcip, p->sp,
                    dstip, p->dp);

        } else {
            syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "[%" PRIu32 ":%" PRIu32 ":%"
                    "" PRIu32 "] %s [Classification: %s] [Priority: %"
                    "" PRIu32 "] {PROTO:%03" PRIu32 "} %s:%" PRIu32 " -> %s:%" PRIu32 "",
                    pa->gid, pa->sid, pa->rev, pa->msg, pa->class_msg,
                    pa->prio, IPV6_GET_L4PROTO(p), srcip, p->sp, dstip, p->dp);
        }

    }
    SCMutexUnlock(&ast->file_ctx->fp_mutex);

    return TM_ECODE_OK;
}

/**
 * \brief   Function which is called to print the decode alerts to the syslog
 *
 * \param tv    Pointer to the threadvars
 * \param p     Pointer to the packet
 * \param data  pointer to the AlertSyslogThread
 * \param pq    pointer the to packet queue
 * \param postpq pointer to the post processed packet queue
 *
 * \return On succes return TM_ECODE_OK
 */
TmEcode AlertSyslogDecoderEvent(ThreadVars *tv, Packet *p, void *data,
                                    PacketQueue *pq, PacketQueue *postpq)
{
    AlertSyslogThread *ast = (AlertSyslogThread *)data;
    int i;

    if (p->alerts.cnt == 0)
        return TM_ECODE_OK;

    SCMutexLock(&ast->file_ctx->fp_mutex);

    ast->file_ctx->alerts += p->alerts.cnt;
    char temp_buf[2048];

    for (i = 0; i < p->alerts.cnt; i++) {
        PacketAlert *pa = &p->alerts.alerts[i];

        syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "[%" PRIu32 ":%" PRIu32 ":%" PRIu32 "]"
                " %s [Classification: %s] [Priority: %" PRIu32 "] [**] [Raw pkt: ",
                pa->gid, pa->sid, pa->rev, pa->msg, pa->class_msg, pa->prio);

        PrintRawLineHexBuf(temp_buf, p->pkt, p->pktlen < 32 ? p->pktlen : 32);
        syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "%s", temp_buf);

        if (p->pcap_cnt != 0) {
            syslog(DEFAULT_ALERT_SYSLOG_LEVEL, "] [pcap file packet: %"PRIu64"]",
                    p->pcap_cnt);
        }

    }
    SCMutexUnlock(&ast->file_ctx->fp_mutex);

    return TM_ECODE_OK;
}

/**
 * \brief   Function which is called to print the alerts to the syslog
 *
 * \param tv    Pointer to the threadvars
 * \param p     Pointer to the packet
 * \param data  pointer to the AlertSyslogThread
 * \param pq    pointer the to packet queue
 * \param postpq pointer to the post processed packet queue
 *
 * \return On succes return TM_ECODE_OK
 */
TmEcode AlertSyslog (ThreadVars *tv, Packet *p, void *data, PacketQueue *pq,
                        PacketQueue *postpq)
{
    if (PKT_IS_IPV4(p)) {
        return AlertSyslogIPv4(tv, p, data, pq, postpq);
    } else if (PKT_IS_IPV6(p)) {
        return AlertSyslogIPv6(tv, p, data, pq, postpq);
    } else if (p->events.cnt > 0) {
        return AlertSyslogDecoderEvent(tv, p, data, pq, postpq);
    }

    return TM_ECODE_OK;
}

/**
 * \brief   Function to print the total alert while closing the engine
 *
 * \param tv    Pointer to the output threadvars
 * \param data  Pointer to the AlertSyslogThread data
 */
void AlertSyslogExitPrintStats(ThreadVars *tv, void *data) {
    AlertSyslogThread *ast = (AlertSyslogThread *)data;
    if (ast == NULL) {
        return;
    }

    SCLogInfo("(%s) Alerts %" PRIu64 "", tv->name, ast->file_ctx->alerts);
}