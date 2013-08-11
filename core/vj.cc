#define _KERNEL

#include <unordered_map>

#include <lockfree/ring.hh>
#include <osv/trace.hh>
#include <osv/poll.h>
#include <osv/types.h>
#include <vj.hh>

#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/tcp.h>
#include <bsd/sys/netinet/ip.h>

#if 0
/*
 * Dump a byte into hex format.
 */
static void
hexbyte(char *buf, uint8_t temp)
{
    uint8_t lo;
    uint8_t hi;

    lo = temp & 0xF;
    hi = temp >> 4;

    if (hi < 10)
        buf[0] = '0' + hi;
    else
        buf[0] = 'A' + hi - 10;

    if (lo < 10)
        buf[1] = '0' + lo;
    else
        buf[1] = 'A' + lo - 10;
}

/*
 * Display a region in traditional hexdump format.
 */
static void
hexdump(const uint8_t *region, uint32_t len)
{
    const uint8_t *line;
    char linebuf[128];
    int i;
    int x;
    int c;

    for (line = region; line < (region + len); line += 16) {

        i = 0;

        linebuf[i] = ' ';
        hexbyte(linebuf + i + 1, ((line - region) >> 8) & 0xFF);
        hexbyte(linebuf + i + 3, (line - region) & 0xFF);
        linebuf[i + 5] = ' ';
        linebuf[i + 6] = ' ';
        i += 7;

        for (x = 0; x < 16; x++) {
          if ((line + x) < (region + len)) {
            hexbyte(linebuf + i,
                *(const u_int8_t *)(line + x));
          } else {
              linebuf[i] = '-';
              linebuf[i + 1] = '-';
            }
            linebuf[i + 2] = ' ';
            if (x == 7) {
              linebuf[i + 3] = ' ';
              i += 4;
            } else {
              i += 3;
            }
        }
        linebuf[i] = ' ';
        linebuf[i + 1] = '|';
        i += 2;
        for (x = 0; x < 16; x++) {
            if ((line + x) < (region + len)) {
                c = *(const u_int8_t *)(line + x);
                /* !isprint(c) */
                if ((c < ' ') || (c > '~'))
                    c = '.';
                linebuf[i] = c;
            } else {
                linebuf[i] = ' ';
            }
            i++;
        }
        linebuf[i] = '|';
        linebuf[i + 1] = 0;
        i += 2;
        puts(linebuf);
    }
}
#endif

TRACEPOINT(trace_vj_classifier_cls_add, "(%d,%d,%d,%d,%d)->%p", in_addr_t, in_addr_t, u8, u16, u16, struct socket*);
TRACEPOINT(trace_vj_classifier_cls_remove, "(%d,%d,%d,%d,%d)", in_addr_t, in_addr_t, u8, u16, u16);
TRACEPOINT(trace_vj_classifier_cls_lookup_found, "(%d,%d,%d,%d,%d)", in_addr_t, in_addr_t, u8, u16, u16);
TRACEPOINT(trace_vj_classifier_cls_lookup_not_found, "(%d,%d,%d,%d,%d)", in_addr_t, in_addr_t, u8, u16, u16);
TRACEPOINT(trace_vj_classifier_packet_delivered, "%p", struct mbuf*);
TRACEPOINT(trace_vj_classifier_poll_wake, "%p", struct mbuf*);
TRACEPOINT(trace_vj_classifier_packet_not_delivered, "%p -> %d", struct mbuf*, int);
TRACEPOINT(trace_vj_classifier_packet_not_delivered_not_tcp, "%p, protocol=%d, len=%d", struct mbuf*, int, int);
TRACEPOINT(trace_vj_classifier_packet_dropped, "");
TRACEPOINT(trace_vj_classifier_packet_popped, "%p", struct mbuf*);
TRACEPOINT(trace_vj_classifier_waiting, "");
TRACEPOINT(trace_vj_classifier_done_waiting, "");

const int rcv_ring_size = 1024;
typedef ring_spsc_waiter<struct mbuf*, rcv_ring_size> vj_ring_type;

static vj_ringbuf ringbuf_to_c(vj_ring_type* ring)
{
    return reinterpret_cast<vj_ringbuf>(ring);
}

static vj_ring_type* ringbuf_from_c(vj_ringbuf ring)
{
    return reinterpret_cast<vj_ring_type*>(ring);
}

static vj_classifier classifier_to_c(vj::classifier* cls)
{
    return reinterpret_cast<vj_classifier>(cls);
}

static vj::classifier* classifier_from_c(vj_classifier cls)
{
    return reinterpret_cast<vj::classifier*>(cls);
}

namespace vj {

    classifier::classifier()
    {

    }

    classifier::~classifier()
    {

    }

    void classifier::add(struct in_addr src_ip, struct in_addr dst_ip,
        u8 ip_proto, u16 src_port, u16 dst_port, struct socket* so)
    {
        trace_vj_classifier_cls_add(src_ip.s_addr, dst_ip.s_addr,
            ip_proto, src_port, dst_port, so);

        classifer_control_msg * cmsg = new classifer_control_msg();
        cmsg->next = nullptr;
        cmsg->ht.src_ip = src_ip;
        cmsg->ht.dst_ip = dst_ip;
        cmsg->ht.ip_proto = ip_proto;
        cmsg->ht.src_port = src_port;
        cmsg->ht.dst_port = dst_port;
        cmsg->so = so;
        cmsg->type = classifer_control_msg::ADD;

        _cls_control.push(cmsg);
    }


    void classifier::remove(struct in_addr src_ip, struct in_addr dst_ip,
        u8 ip_proto, u16 src_port, u16 dst_port)
    {
        trace_vj_classifier_cls_remove(src_ip.s_addr, dst_ip.s_addr,
            ip_proto, src_port, dst_port);

        classifer_control_msg * cmsg = new classifer_control_msg();
        cmsg->next = nullptr;
        cmsg->ht.src_ip = src_ip;
        cmsg->ht.dst_ip = dst_ip;
        cmsg->ht.ip_proto = ip_proto;
        cmsg->ht.src_port = src_port;
        cmsg->ht.dst_port = dst_port;
        cmsg->type = classifer_control_msg::REMOVE;

        _cls_control.push(cmsg);
    }

    void classifier::process_control(void)
    {
        struct classifer_control_msg * item;
        while ((item = _cls_control.pop())) {
            switch (item->type) {
            case classifer_control_msg::ADD:
                _classifications.insert(std::pair<vj_hashed_tuple, struct socket*>(item->ht, item->so));
                break;
            case classifer_control_msg::REMOVE:
                _classifications.erase(item->ht);
                break;
            default:
                debug("vj: unknown classification\n");
            }
            delete item;
        }
    }

    struct socket* classifier::lookup(struct in_addr src_ip, struct in_addr dst_ip,
        u8 ip_proto, u16 src_port, u16 dst_port)
    {
        vj_hashed_tuple ht;
        ht.src_ip = src_ip;
        ht.dst_ip = dst_ip;
        ht.ip_proto = ip_proto;
        ht.src_port = src_port;
        ht.dst_port = dst_port;

        auto it = _classifications.find(ht);
        if (it == _classifications.end()) {
            trace_vj_classifier_cls_lookup_not_found(src_ip.s_addr,
                dst_ip.s_addr, ip_proto, src_port, dst_port);
            return nullptr;
        }

        trace_vj_classifier_cls_lookup_found(src_ip.s_addr, dst_ip.s_addr,
            ip_proto, src_port, dst_port);
        return (it->second);
    }

    bool classifier::try_deliver(struct mbuf* m)
    {
        struct in_addr src_ip;
        struct in_addr dst_ip;
        u8 ip_proto;
        u16 src_port;
        u16 dst_port;

        // Test packet length
        if (m->m_len < (int)(ETHER_HDR_LEN + sizeof(struct ip))) {
            trace_vj_classifier_packet_not_delivered(m, 1);
            return false;
        }

        // Basic decode
        u8* pkt = mtod(m, u8*);
        struct ip* ip = reinterpret_cast<struct ip*>(pkt + ETHER_HDR_LEN);
        u8 hlen = ip->ip_hl << 2;
        src_ip = ip->ip_src;
        dst_ip = ip->ip_dst;
        ip_proto = ip->ip_p;

        // Make sure it's a TCP packet and that it has space to read the
        // TCP header
        if ((ip_proto != IPPROTO_TCP) ||
            (m->m_len < (int)(ETHER_HDR_LEN + hlen + sizeof(struct tcphdr)))) {
            trace_vj_classifier_packet_not_delivered_not_tcp(m, ip_proto, m->m_len);
            return false;
        }

        // Process control messages
        process_control();

        struct tcphdr* tcp = reinterpret_cast<struct tcphdr*>(pkt + ETHER_HDR_LEN + hlen);
        src_port = tcp->th_sport;
        dst_port = tcp->th_dport;

        struct socket* so = lookup(dst_ip, src_ip, ip_proto, dst_port, src_port);
        if (so == nullptr) {
//            uint8_t* packet = mtod(m, uint8_t*);
//            puts("Packet lookup failed:\n");
//            hexdump(packet, m->m_len);
//            puts("----\n");
            trace_vj_classifier_packet_not_delivered(m, 3);
            return false;
        }

        vj_ring_type* ring = ringbuf_from_c(so->so_rcv.sb_ring);
        bool rc = ring->push(m);
        if (!rc) {
            trace_vj_classifier_packet_dropped();
            m_free(m);
            return true;
        }

        trace_vj_classifier_packet_delivered(m);

        // Wake up user in case it is waiting
        ring->wake_consumer();

        // If the user thread is being polled, wake it up so it could process
        // the incoming packets
        if (so->so_rcv.sb_flags & SB_SEL) {
            trace_vj_classifier_poll_wake(m);
            poll_wake(so->fp, POLL_VJ);
        }

        return true;
    }

} // namespace vj


vj_ringbuf vj_ringbuf_create()
{
    return ringbuf_to_c(new vj_ring_type());
}

struct mbuf* vj_ringbuf_pop(vj_ringbuf ringbuf)
{
    vj_ring_type* ring = ringbuf_from_c(ringbuf);
    struct mbuf* result = nullptr;

    ring->pop(result);
    trace_vj_classifier_packet_popped(result);

    return (result);
}

void vj_ringbuf_destroy(vj_ringbuf ringbuf)
{
    vj_ring_type* ring = ringbuf_from_c(ringbuf);
    delete ring;
}


void vj_wait(vj_ringbuf ringbuf)
{
    vj_ring_type* ring = ringbuf_from_c(ringbuf);
    trace_vj_classifier_waiting();
    ring->wait_for_items();
    trace_vj_classifier_done_waiting();
}

//////////////////////////////////////////////////////////////////////////////


vj_classifier vj_classifier_create()
{
    return classifier_to_c(new vj::classifier());
}

void vj_classify_remove(vj_classifier cls, struct in_addr laddr, struct in_addr faddr,
    u_char ip_p, u_int16_t lport, u_int16_t fport)
{
    classifier_from_c(cls)->remove(laddr, faddr, ip_p, lport, fport);
}

void vj_classify_add(vj_classifier cls,
    struct in_addr laddr, struct in_addr faddr,
    u_char ip_p, u_int16_t lport, u_int16_t fport, struct socket* so)
{
    vj::classifier* obj = classifier_from_c(cls);
    if (obj)
        obj->add(laddr, faddr, ip_p, lport, fport, so);
}

int vj_try_deliver(vj_classifier cls, struct mbuf* m)
{
    vj::classifier* obj = classifier_from_c(cls);
    assert(obj != nullptr);
    if (obj)
        return (obj->try_deliver(m) ? 1 : 0);

    return 0;
}



