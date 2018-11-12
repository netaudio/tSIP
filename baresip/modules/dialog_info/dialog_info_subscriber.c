/** \file
*/
#include <re.h>
#include <baresip.h>
#include "dialog_info.h"
#include <re_sxmlc.h>

#define DEBUG_MODULE "dialog-info"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


struct dialog_info {
	struct le le;
	struct sipsub *sub;
	struct tmr tmr;
	enum dialog_info_status status;
	unsigned failc;
	struct contact *contact;
};

static struct list dialog_infol;


static void tmr_handler(void *arg);


static uint32_t wait_term(const struct sipevent_substate *substate)
{
	uint32_t wait;

	switch (substate->reason) {

	case SIPEVENT_DEACTIVATED:
	case SIPEVENT_TIMEOUT:
		wait = 5;
		break;

	case SIPEVENT_REJECTED:
	case SIPEVENT_NORESOURCE:
		wait = 3600;
		break;

	case SIPEVENT_PROBATION:
	case SIPEVENT_GIVEUP:
	default:
		wait = 300;
		if (pl_isset(&substate->retry_after))
			wait = max(wait, pl_u32(&substate->retry_after));
		break;
	}

	return wait;
}


static uint32_t wait_fail(unsigned failc)
{
	switch (failc) {

	case 1:  return 30;
	case 2:  return 300;
	case 3:  return 3600;
	default: return 86400;
	}
}

static const char* STR_EARLY = "early";
static const char* STR_CONFIRMED = "confirmed";
static const char* STR_TERMINATED = "terminated";

struct dialog_info_context {
	bool error;
	bool in_dialog_info;
	bool in_dialog;
	bool in_remote;
	bool in_identity;
	enum dialog_info_direction direction;	
	char identity_display[64];
	char identity[64];
	enum dialog_info_status status;	
};

static int dialog_info_sxmlc_callback(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	struct dialog_info_context* ctx = (struct dialog_info_context*)sd->user;

	switch(evt) {
		case XML_EVENT_START_NODE:
			if (!ctx->in_dialog_info) {
				if (!stricmp(node->tag, C2SX("dialog-info"))) {
					ctx->in_dialog_info = true;
					// FreeSWITCH interoperability: after subscribing there is no "dialog" element if extension is idle
					// (which actually makes sense)
					// => assuming that extension is in "terminated" state by default
					ctx->status = DIALOG_INFO_TERMINATED;
				}
			}
			if (!ctx->in_dialog_info)
				break;
			if (!ctx->in_dialog) {
				if (!stricmp(node->tag, C2SX("dialog"))) {
					SXML_CHAR* val = NULL;
					ctx->in_dialog = true;
					if (XMLNode_get_attribute_with_default(node, C2SX("direction"), &val, NULL) != false) {
						if (val) {
							if (!strcmp(val, "initiator")) {
								ctx->direction = DIALOG_INFO_DIR_INITIATOR;
							} else if (!strcmp(val, "recipient")) {
								ctx->direction = DIALOG_INFO_DIR_RECIPIENT;
							}
							__free(val);
						}
					}
				}
			}
			if (!ctx->in_dialog)
				break;
			if (!ctx->in_remote) {
				if (!stricmp(node->tag, C2SX("remote")))
					ctx->in_remote = true;
			}
			if (!ctx->in_remote)
				break;
			if (!ctx->in_identity) {
				if (!stricmp(node->tag, C2SX("identity"))) {
					SXML_CHAR* val = NULL;
					ctx->in_identity = true;
					if (XMLNode_get_attribute_with_default(node, C2SX("display"), &val, NULL) != false) {
                    	if (val) {
							strncpy(ctx->identity_display, val, sizeof(ctx->identity_display));
							ctx->identity_display[sizeof(ctx->identity_display)-1] = '\0';
                        }
					}
					if (val) {
						__free(val);
                    }
				}
			}
			if (!ctx->in_identity)
				break;
			
			break;

		case XML_EVENT_END_NODE:
			if (!stricmp(node->tag, C2SX("dialog-info"))) return false;
			if (!stricmp(node->tag, C2SX("dialog"))) return false;
			if (!stricmp(node->tag, C2SX("remote"))) return false;
			if (ctx->in_remote) {
				if (!stricmp(node->tag, C2SX("identity"))) return false;
			}
			break;

		case XML_EVENT_TEXT:
			if (ctx->in_identity) {
				strncpy(ctx->identity, text, sizeof(ctx->identity));
				ctx->identity[sizeof(ctx->identity)-1] = '\0';
			}
			break;

		case XML_EVENT_ERROR:
			ctx->error = true;
			return false;
			
		default:
			break;
	}

	
	return true;
}

static void notify_handler(struct sip *sip, const struct sip_msg *msg,
			   void *arg)
{
	enum dialog_info_status status = DIALOG_INFO_UNKNOWN;
	struct dialog_info *dlg_info = arg;
	const struct sip_hdr *hdr;
	struct pl pl_remote_identity, pl_remote_identity_display;
	struct dialog_info_context ctx;
	SAX_Callbacks sax;


	dlg_info->failc = 0;

	hdr = sip_msg_hdr(msg, SIP_HDR_CONTENT_TYPE);
	if (!hdr || 0 != pl_strcasecmp(&hdr->val, "application/dialog-info+xml")) {

		if (hdr)
			DEBUG_WARNING("dialog-info: unsupported content-type: '%r'\n",
				&hdr->val);

		sip_treplyf(NULL, NULL, sip, msg, false,
			    415, "Unsupported Media Type",
			    "Accept: application/dialog-info+xml\r\n"
			    "Content-Length: 0\r\n"
				"\r\n");
		return;
	}

	/** \note Example notify body:

	<?xml version="1.0" encoding="UTF-8"?>
	<dialog-info xmlns="urn:ietf:params:xml:ns:dialog-info" version="0" state="full" entity="sip:%23300@192.168.0.176">
		<dialog id="sip:%23300@192.168.0.176">
			<state>early</state>
		</dialog>
	</dialog-info>

	*/

	/** \note Example body with remote identity as generated by FreeSWITCH:

	<?xml version="1.0"?>
	<dialog-info xmlns="urn:ietf:params:xml:ns:dialog-info" version="12" state="full" entity="sip:11@pbx.xxxx.net">
		<dialog id="xxxxx-xxxx-xxxx-xxxx-ff486aabfe49" direction="initiator">
			<state>confirmed</state>
			<local>
				<identity display="11">sip:11@pbx.xxxx.net</identity>
				<target uri="sip:11@pbx.xxxx.net">
					<param pname="+sip.rendering" pvalue="yes"/>
				</target>
			</local>
			<remote>
				<identity display="0310000">sip:0310000@pbx.xxxx.net</identity>
				<target uri="sip:**11@pbx.xxxx.net"/>
			</remote>
		</dialog>
	</dialog-info>

	\note direction="initiator"/"recipient"
	
    */

	/** \todo This should be replaced with newer code using proper XML parsing */
	if (!re_regex((const char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb), STR_EARLY)) {
		status = DIALOG_INFO_EARLY;
	} else if (!re_regex((const char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb), STR_CONFIRMED)) {
		status = DIALOG_INFO_CONFIRMED;
	} else if (!re_regex((const char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb), STR_TERMINATED)) {
		status = DIALOG_INFO_TERMINATED;
	} else {
		status = DIALOG_INFO_UNKNOWN;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.status = DIALOG_INFO_UNKNOWN;
	SAX_Callbacks_init(&sax);
	sax.all_event = dialog_info_sxmlc_callback;

#if 1
	XMLDoc_parse_buffer_SAX_len((const char *)mbuf_buf(msg->mb), mbuf_get_left(msg->mb), "root", &sax, &ctx);
#else
	// test
	{
	#if 1
		const char* body =
			"\t<?xml version=\"1.0\"?>\n"
			"\t<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"12\" state=\"full\" entity=\"sip:11@pbx.xxxx.net\">\n"
			"\t\t<dialog id=\"xxxxx-xxxx-xxxx-xxxx-ff486aabfe49\" direction=\"initiator\">\n"
			"\t\t\t<state>confirmed</state>\n"
			"\t\t\t<local>\n"
			"\t\t\t\t<identity display=\"11\">sip:11@pbx.xxxx.net</identity>\n"
			"\t\t\t\t<target uri=\"sip:11@pbx.xxxx.net\">\n"
			"\t\t\t\t\t<param pname=\"+sip.rendering\" pvalue=\"yes\"/>\n"
			"\t\t\t\t</target>\n"
			"\t\t\t</local>\n"
			"\t\t\t<remote>\n"
			"\t\t\t\t<identity display=\"0310000\">sip:0310000@pbx.xxxx.net</identity>\n"
			"\t\t\t\t<target uri=\"sip:**11@pbx.xxxx.net\"/>\n"
			"\t\t\t</remote>\n"
			"\t\t</dialog>\n"
			"\t</dialog-info>"
			;
	#else
		const char* body = "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"0\" state=\"full\" entity=\"sip:12@pbx.xxx.net\"></dialog-info>";
	#endif
		XMLDoc_parse_buffer_SAX_len(body, strlen(body), "root", &sax, &ctx);
	}
#endif
	(void)sip_treply(NULL, sip, msg, 200, "OK");

	pl_set_str(&pl_remote_identity, ctx.identity);
	pl_set_str(&pl_remote_identity_display, ctx.identity_display);
	if (status == DIALOG_INFO_UNKNOWN)
	{
        status = ctx.status;
    }
	contact_set_dialog_info(dlg_info->contact, status, ctx.direction, &pl_remote_identity, &pl_remote_identity_display);
}


static void close_handler(int err, const struct sip_msg *msg,
			  const struct sipevent_substate *substate, void *arg)
{
	struct dialog_info *dlg_info = arg;
	uint32_t wait;

	dlg_info->sub = mem_deref(dlg_info->sub);

	DEBUG_INFO("dialog-info: subscriber closed <%r>: ",
	     &contact_addr(dlg_info->contact)->auri);

	if (substate) {
		DEBUG_INFO("%s", sipevent_reason_name(substate->reason));
		wait = wait_term(substate);
	}
	else if (msg) {
		DEBUG_INFO("%u %r", msg->scode, &msg->reason);
		wait = wait_fail(++dlg_info->failc);
	}
	else {
		DEBUG_INFO("%m", err);
		wait = wait_fail(++dlg_info->failc);
	}

	DEBUG_INFO("; will retry in %u secs (failc=%u)\n", wait, dlg_info->failc);

	tmr_start(&dlg_info->tmr, wait * 1000, tmr_handler, dlg_info);

	contact_set_dialog_info(dlg_info->contact, DIALOG_INFO_UNKNOWN, DIALOG_INFO_DIR_UNKNOWN, &pl_null, &pl_null);
}


static void destructor(void *arg)
{
	struct dialog_info *dlg_info = arg;

	list_unlink(&dlg_info->le);
	tmr_cancel(&dlg_info->tmr);
	mem_deref(dlg_info->contact);
	mem_deref(dlg_info->sub);
}


static int auth_handler(char **username, char **password,
			const char *realm, void *arg)
{
	return account_auth(arg, username, password, realm);
}


static int subscribe(struct dialog_info *dlg_info)
{
	const char *routev[1];
	struct ua *ua;
	char uri[256];
	int err;

	/* We use the first UA */
	ua = uag_find_aor(NULL);
	if (!ua) {
		DEBUG_WARNING("dialog-info: no UA found\n");
		return ENOENT;
	}

	pl_strcpy(&contact_addr(dlg_info->contact)->auri, uri, sizeof(uri));

	routev[0] = ua_outbound(ua);

	err = sipevent_subscribe(&dlg_info->sub, uag_sipevent_sock(), uri, NULL,
				 ua_aor(ua), "dialog", "application/dialog-info+xml", NULL, 600,
				 ua_cuser(ua), routev, routev[0] ? 1 : 0,
				 auth_handler, ua_prm(ua), true, NULL,
				 notify_handler, close_handler, dlg_info,
				 "%H", ua_print_supported, ua);
	if (err) {
		DEBUG_WARNING("dialog-info: sipevent_subscribe failed: %m\n", err);
	}

	return err;
}


static void tmr_handler(void *arg)
{
	struct dialog_info *dlg_info = arg;

	if (subscribe(dlg_info)) {
		tmr_start(&dlg_info->tmr, wait_fail(++dlg_info->failc) * 1000,
			  tmr_handler, dlg_info);
	}
}


static int dialog_info_alloc(struct contact *contact)
{
	struct dialog_info *dlg_info;

	dlg_info = mem_zalloc(sizeof(*dlg_info), destructor);
	if (!dlg_info)
		return ENOMEM;

	dlg_info->status  = DIALOG_INFO_UNKNOWN;
	dlg_info->contact = mem_ref(contact);

	tmr_init(&dlg_info->tmr);
	tmr_start(&dlg_info->tmr, 1000, tmr_handler, dlg_info);

	list_append(&dialog_infol, &dlg_info->le, dlg_info);

	return 0;
}


int dialog_info_subscriber_init(void)
{
	struct le *le;
	int err = 0;

	for (le = list_head(contact_list()); le; le = le->next) {

		struct contact *c = le->data;
		struct sip_addr *addr = contact_addr(c);
		struct pl val;

		if (0 == sip_param_decode(&addr->params, "dlginfo", &val) &&
		    0 == pl_strcasecmp(&val, "p2p")) {

			err |= dialog_info_alloc(le->data);
		}
	}

	DEBUG_INFO("Subscribing dialog-info to %u contacts\n", list_count(&dialog_infol));

	return err;
}


void dialog_info_subscriber_close(void)
{
	list_flush(&dialog_infol);
}
