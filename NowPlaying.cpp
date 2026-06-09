// NowPlaying.cpp  –  VirtualDJ General plugin  v4.0
//
// Subscription-based WebSocket server.
//
// Protocol:
//   Client → Plugin (once after connect):
//     {"subscribe":{"numeric":["deck 1 get_pos",...],"string":["deck 1 get_title",...],"token":"secret"}}
//
//   Plugin → Client (every 100ms):
//     {"deck 1 get_pos":0.7534,"deck 1 get_title":"Song Name",...}
//
// Config (Documents/VirtualDJ/Plugins8/NowPlaying.ini):
//   [NowPlaying]
//   Port=9001
//   AllowedOrigins=*
//   AllowedVerbs=get_title,get_artist,get_remix_after_title,get_key,get_bpm,get_time,get_level,get_volume,eq_high,eq_mid,eq_low,filter,play,crossfader,get_pos
//   AuthToken=
//
//   AllowedOrigins: comma-separated list of allowed Origin headers, or * for any.
//   AllowedVerbs:   comma-separated verb stems. "deck N " prefix is stripped before matching.
//                   A stem of "get_time" matches "get_time", "get_time \"elapsed\"", etc.
//   AuthToken:      if non-empty, clients must include "token":"<value>" in their subscribe message.

#include "sdk/vdjPlugin8.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")

#define MAX_CLIENTS   16
#define POLL_MS       100
#define MAX_QUERY_LEN 96
#define MAX_QUERIES   40
#define MAX_VERBS     64

// ─── Runtime config (loaded from INI in OnLoad) ───────────────────────────────
static int  g_port            = 9001;
static char g_allowedOrigins[1024] = "*";
static char g_authToken[256]       = "";
static char g_verbs[MAX_VERBS][MAX_QUERY_LEN];
static int  g_nVerbs          = 0;

static const char *DEFAULT_VERBS =
    "get_title,get_artist,get_remix_after_title,get_key,get_bpm,"
    "get_time,get_level,get_volume,eq_high,eq_mid,eq_low,filter,"
    "play,crossfader,get_pos,get_position";

static void parse_verb_list(const char *csv)
{
    g_nVerbs = 0;
    const char *p = csv;
    while (*p && g_nVerbs < MAX_VERBS) {
        while (*p == ' ' || *p == '\t') p++;
        int i = 0;
        while (*p && *p != ',' && *p != '\r' && *p != '\n' && i < MAX_QUERY_LEN-1)
            g_verbs[g_nVerbs][i++] = *p++;
        while (i > 0 && g_verbs[g_nVerbs][i-1] == ' ') i--;
        g_verbs[g_nVerbs][i] = '\0';
        if (i > 0) g_nVerbs++;
        if (*p == ',') p++;
    }
}

// Strip "deck N " prefix, return pointer into query at the verb part
static const char *verb_part(const char *query)
{
    if (strncmp(query, "deck ", 5) != 0) return query;
    const char *p = query + 5;
    while (*p && *p != ' ') p++;  // skip deck number
    return (*p == ' ') ? p + 1 : p;
}

// Returns true if query is allowed by the whitelist (empty whitelist = allow all)
static bool verb_allowed(const char *query)
{
    if (g_nVerbs == 0) return true;
    const char *verb = verb_part(query);
    for (int i = 0; i < g_nVerbs; i++) {
        int len = (int)strlen(g_verbs[i]);
        if (strncmp(verb, g_verbs[i], len) == 0) {
            char next = verb[len];
            if (next == '\0' || next == ' ' || next == '"') return true;
        }
    }
    return false;
}

static bool origin_allowed(const char *origin)
{
    if (!origin || origin[0] == '\0') return true;  // no Origin header = local/native
    if (strcmp(g_allowedOrigins, "*") == 0) return true;
    const char *p = g_allowedOrigins;
    int olen = (int)strlen(origin);
    while (*p) {
        while (*p == ' ') p++;
        if (strncmp(p, origin, olen) == 0) {
            char next = p[olen];
            if (next == '\0' || next == ',' || next == ' ') return true;
        }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return false;
}

// ─── Per-client state ─────────────────────────────────────────────────────────
struct QueryEntry {
    char q[MAX_QUERY_LEN];
    bool isString;
};

struct Client {
    SOCKET     sock;
    QueryEntry queries[MAX_QUERIES];
    int        nQueries;
    bool       subscribed;
};

// ─── JSON string escape ───────────────────────────────────────────────────────
static void jstr(char *dst, int max, const char *src)
{
    int j = 0;
    for (int i = 0; src[i] && j < max-4; i++) {
        unsigned char c = src[i];
        if      (c == '"')  { dst[j++]='\\'; dst[j++]='"';  }
        else if (c == '\\') { dst[j++]='\\'; dst[j++]='\\'; }
        else if (c == '\n') { dst[j++]='\\'; dst[j++]='n';  }
        else if (c == '\r') { dst[j++]='\\'; dst[j++]='r';  }
        else if (c >= 0x20)   dst[j++]=c;
    }
    dst[j] = '\0';
}

// ─── Base64 ───────────────────────────────────────────────────────────────────
static void b64enc(const unsigned char *src, int len, char *dst)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i=0, j=0;
    while (i < len) {
        int a=src[i++], b=i<len?src[i++]:0, c=i<len?src[i++]:0;
        dst[j++]=T[a>>2]; dst[j++]=T[((a&3)<<4)|(b>>4)];
        dst[j++]=T[((b&15)<<2)|(c>>6)]; dst[j++]=T[c&63];
    }
    int rem = len%3;
    if (rem==1){ dst[j-2]='='; dst[j-1]='='; } else if(rem==2){ dst[j-1]='='; }
    dst[j]='\0';
}

// ─── SHA-1 via CNG → base64 ───────────────────────────────────────────────────
static void sha1b64(const char *input, char *out64)
{
    BCRYPT_ALG_HANDLE  hAlg  = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    DWORD objLen=0, dummy=0;
    unsigned char hash[20]={};
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, NULL, 0);
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(DWORD), &dummy, 0);
    PUCHAR obj = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, objLen);
    BCryptCreateHash(hAlg, &hHash, obj, objLen, NULL, 0, 0);
    BCryptHashData(hHash, (PUCHAR)input, (ULONG)strlen(input), 0);
    BCryptFinishHash(hHash, hash, 20, 0);
    BCryptDestroyHash(hHash);
    HeapFree(GetProcessHeap(), 0, obj);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    b64enc(hash, 20, out64);
}

// ─── WebSocket handshake (with Origin check) ──────────────────────────────────
static bool ws_handshake(SOCKET sock)
{
    char buf[2048]={};
    if (recv(sock, buf, sizeof(buf)-1, 0) <= 0) return false;

    // Extract and check Origin header
    char origin[256]={};
    char *oh = strstr(buf, "Origin:");
    if (!oh) oh = strstr(buf, "origin:");
    if (oh) {
        oh += 7;
        while (*oh == ' ') oh++;
        int i = 0;
        while (*oh && *oh != '\r' && *oh != '\n' && i < 255) origin[i++] = *oh++;
        origin[i] = '\0';
    }
    if (!origin_allowed(origin)) return false;

    char *p = strstr(buf, "Sec-WebSocket-Key:");
    if (!p) return false;
    p += 18;
    while (*p==' ') p++;
    char key[128]={};
    int ki=0;
    while (*p && *p!='\r' && *p!='\n' && ki<120) key[ki++]=*p++;

    char combined[256]={};
    snprintf(combined, sizeof(combined), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    char accept[64]={};
    sha1b64(combined, accept);

    char resp[512]={};
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    return send(sock, resp, rlen, 0) == rlen;
}

// ─── WebSocket send/recv ──────────────────────────────────────────────────────
static void ws_send(SOCKET sock, const char *data, int dlen)
{
    unsigned char hdr[4]; int hlen;
    hdr[0] = 0x81;
    if (dlen < 126) { hdr[1]=(unsigned char)dlen; hlen=2; }
    else { hdr[1]=126; hdr[2]=(dlen>>8)&0xFF; hdr[3]=dlen&0xFF; hlen=4; }
    send(sock, (const char*)hdr, hlen, 0);
    send(sock, data, dlen, 0);
}

static int ws_recv_msg(SOCKET sock, char *outBuf, int outBufSize)
{
    u_long avail = 0;
    if (ioctlsocket(sock, FIONREAD, &avail) != 0 || avail == 0) return 0;
    unsigned char hdr[2]={};
    if (recv(sock,(char*)hdr,2,0) != 2) return -1;
    int opcode = hdr[0] & 0x0F;
    int masked  = (hdr[1] & 0x80) != 0;
    int plen    = hdr[1] & 0x7F;
    if (plen == 126) {
        unsigned char ext[2]={};
        if (recv(sock,(char*)ext,2,0) != 2) return -1;
        plen = (ext[0]<<8)|ext[1];
    }
    unsigned char mask[4]={};
    if (masked) recv(sock,(char*)mask,4,0);
    char payload[1024]={};
    int toRead = (plen > (int)sizeof(payload)-1) ? (int)sizeof(payload)-1 : plen;
    if (toRead > 0) recv(sock, payload, toRead, 0);
    if (masked) for(int i=0;i<toRead;i++) payload[i]^=mask[i%4];
    if (opcode == 0x08) return -1;
    if (opcode == 0x09) {
        unsigned char pong[2]={0x8A,(unsigned char)(toRead < 126 ? toRead : 125)};
        send(sock,(char*)pong,2,0);
        if (toRead > 0) send(sock,payload,toRead,0);
        return 0;
    }
    if (opcode == 0x01 || opcode == 0x02) {
        int copyLen = toRead < outBufSize-1 ? toRead : outBufSize-1;
        memcpy(outBuf, payload, copyLen);
        outBuf[copyLen] = '\0';
        return 1;
    }
    return 0;
}

// ─── Subscribe message parser ─────────────────────────────────────────────────
static void parse_string_array(const char *pos, QueryEntry *out, int *count, bool isStr)
{
    pos = strchr(pos, '[');
    if (!pos) return;
    pos++;
    while (*pos && *pos != ']' && *count < MAX_QUERIES) {
        pos = strchr(pos, '"');
        if (!pos || *pos == ']') break;
        pos++;
        QueryEntry &e = out[*count];
        int i = 0;
        while (*pos && i < MAX_QUERY_LEN-1) {
            if (*pos == '\\' && *(pos+1) == '"') { e.q[i++] = '"'; pos += 2; continue; }
            if (*pos == '"') break;
            e.q[i++] = *pos++;
        }
        e.q[i] = '\0';
        e.isString = isStr;
        if (i > 0) (*count)++;
        if (*pos == '"') pos++;
    }
}

// Returns false if auth token is wrong (disconnects client)
static bool parse_subscribe(const char *json, Client &cl)
{
    if (!strstr(json, "\"subscribe\"")) return false;

    // Auth token check
    if (g_authToken[0] != '\0') {
        const char *tp = strstr(json, "\"token\"");
        if (!tp) return false;
        tp = strchr(tp + 7, '"');
        if (!tp) return false;
        tp++;
        char provided[256]={};
        int i = 0;
        while (*tp && *tp != '"' && i < 255) provided[i++] = *tp++;
        provided[i] = '\0';
        if (strcmp(provided, g_authToken) != 0) return false;
    }

    cl.nQueries = 0;
    const char *p = strstr(json, "\"numeric\"");
    if (p) parse_string_array(p, cl.queries, &cl.nQueries, false);
    p = strstr(json, "\"string\"");
    if (p) parse_string_array(p, cl.queries, &cl.nQueries, true);

    // Filter out verbs not in the whitelist
    int kept = 0;
    for (int i = 0; i < cl.nQueries; i++) {
        if (verb_allowed(cl.queries[i].q)) {
            if (kept != i) cl.queries[kept] = cl.queries[i];
            kept++;
        }
    }
    cl.nQueries = kept;

    return (cl.nQueries > 0);
}

// ─── Plugin class ─────────────────────────────────────────────────────────────
class NowPlayingPlugin : public IVdjPlugin8
{
public:
    NowPlayingPlugin() : hThread(NULL), running(false) {}

    HRESULT VDJ_API OnLoad() override
    {
        // Find INI next to the DLL itself
        char iniPath[600]={};
        HMODULE hm = NULL;
        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)&g_port, &hm)) {
            char dllPath[512]={};
            GetModuleFileName(hm, dllPath, sizeof(dllPath));
            char *sep = strrchr(dllPath, '\\');
            if (sep) { sep[1]='\0'; snprintf(iniPath, sizeof(iniPath), "%sNowPlaying.ini", dllPath); }
        }

        g_port = (int)GetPrivateProfileInt("NowPlaying", "Port", 9001, iniPath);

        GetPrivateProfileString("NowPlaying", "AllowedOrigins", "*",
            g_allowedOrigins, sizeof(g_allowedOrigins), iniPath);

        GetPrivateProfileString("NowPlaying", "AuthToken", "",
            g_authToken, sizeof(g_authToken), iniPath);

        char verbsStr[4096]={};
        GetPrivateProfileString("NowPlaying", "AllowedVerbs", DEFAULT_VERBS,
            verbsStr, sizeof(verbsStr), iniPath);
        parse_verb_list(verbsStr);

        running = true;
        hThread = CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
        return S_OK;
    }

    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8 *info) override
    {
        info->PluginName  = "NowPlaying";
        info->Author      = "tr1cks";
        info->Description = "Subscription WebSocket server";
        info->Version     = "4.0";
        info->Bitmap      = NULL;
        info->Flags       = VDJFLAG_NODOCK;
        return S_OK;
    }

    ULONG VDJ_API Release() override
    {
        running = false;
        if (hThread) { WaitForSingleObject(hThread, 4000); CloseHandle(hThread); hThread = NULL; }
        delete this;
        return S_OK;
    }

private:
    HANDLE        hThread;
    volatile bool running;

    void BuildLegacyJson(char *buf, int bufSize)
    {
        char t1[512]={}, a1[512]={}, k1[64]={};
        char t2[512]={}, a2[512]={}, k2[64]={};
        double bpm1=0, pos1=0, play1=0, bpm2=0, pos2=0, play2=0, cf=0.5;
        GetStringInfo("deck 1 get_title",  t1, sizeof(t1));
        GetStringInfo("deck 1 get_artist", a1, sizeof(a1));
        GetStringInfo("deck 1 get_key",    k1, sizeof(k1));
        GetInfo("deck 1 get_bpm", &bpm1); GetInfo("deck 1 get_pos", &pos1); GetInfo("deck 1 play", &play1);
        GetStringInfo("deck 2 get_title",  t2, sizeof(t2));
        GetStringInfo("deck 2 get_artist", a2, sizeof(a2));
        GetStringInfo("deck 2 get_key",    k2, sizeof(k2));
        GetInfo("deck 2 get_bpm", &bpm2); GetInfo("deck 2 get_pos", &pos2); GetInfo("deck 2 play", &play2);
        GetInfo("crossfader", &cf);
        char et1[1024]={},ea1[1024]={},ek1[128]={},et2[1024]={},ea2[1024]={},ek2[128]={};
        jstr(et1,sizeof(et1),t1); jstr(ea1,sizeof(ea1),a1); jstr(ek1,sizeof(ek1),k1);
        jstr(et2,sizeof(et2),t2); jstr(ea2,sizeof(ea2),a2); jstr(ek2,sizeof(ek2),k2);
        snprintf(buf, bufSize,
            "{\"deck1\":{\"title\":\"%s\",\"artist\":\"%s\",\"bpm\":%.1f,\"key\":\"%s\",\"pos\":%.4f,\"playing\":%d},"
            "\"deck2\":{\"title\":\"%s\",\"artist\":\"%s\",\"bpm\":%.1f,\"key\":\"%s\",\"pos\":%.4f,\"playing\":%d},"
            "\"crossfader\":%.3f}",
            et1,ea1,bpm1>0?bpm1:0.0,ek1,pos1>0?pos1:0.0,(play1>0.5)?1:0,
            et2,ea2,bpm2>0?bpm2:0.0,ek2,pos2>0?pos2:0.0,(play2>0.5)?1:0,
            (cf>=0&&cf<=1)?cf:0.5);
    }

    void BuildClientJson(char *buf, int bufSize, const Client &cl)
    {
        int w = 0;
        buf[w++] = '{';
        for (int i = 0; i < cl.nQueries; i++) {
            const QueryEntry &qe = cl.queries[i];
            char escKey[128]={};
            jstr(escKey, sizeof(escKey), qe.q);
            w += snprintf(buf+w, bufSize-w, "%s\"%s\":", i>0?",":"", escKey);
            if (!qe.isString) {
                double val = 0.0;
                GetInfo(qe.q, &val);
                w += snprintf(buf+w, bufSize-w, "%.6g", val);
            } else {
                char raw[512]={};
                GetStringInfo(qe.q, raw, sizeof(raw));
                char esc[1024]={};
                jstr(esc, sizeof(esc), raw);
                w += snprintf(buf+w, bufSize-w, "\"%s\"", esc);
            }
        }
        if (w < bufSize-1) buf[w++] = '}';
        buf[w] = '\0';
    }

    void ServerLoop()
    {
        WSADATA wsa={};
        WSAStartup(MAKEWORD(2,2), &wsa);

        SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
        int opt=1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        struct sockaddr_in addr={};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = htons((u_short)g_port);

        if (bind(srv,(sockaddr*)&addr,sizeof(addr))!=0 || listen(srv,5)!=0) {
            closesocket(srv); WSACleanup(); return;
        }
        u_long nb=1; ioctlsocket(srv, FIONBIO, &nb);

        Client clients[MAX_CLIENTS];
        int    nClients = 0;
        for (int i=0; i<MAX_CLIENTS; i++) {
            clients[i].sock=INVALID_SOCKET; clients[i].nQueries=0; clients[i].subscribed=false;
        }

        DWORD lastPoll = 0;
        while (running) {
            DWORD now = GetTickCount();
            if (now - lastPoll >= POLL_MS) {
                lastPoll = now;
                char legacyJson[4096]={}; bool legacyBuilt=false;
                for (int i=0; i<nClients; i++) {
                    if (clients[i].sock == INVALID_SOCKET) continue;
                    if (!clients[i].subscribed) {
                        if (!legacyBuilt) { BuildLegacyJson(legacyJson, sizeof(legacyJson)); legacyBuilt=true; }
                        ws_send(clients[i].sock, legacyJson, (int)strlen(legacyJson));
                    } else {
                        char json[8192]={};
                        BuildClientJson(json, sizeof(json), clients[i]);
                        ws_send(clients[i].sock, json, (int)strlen(json));
                    }
                }
            }

            SOCKET newSock = accept(srv, NULL, NULL);
            if (newSock != INVALID_SOCKET) {
                u_long blk=0; ioctlsocket(newSock, FIONBIO, &blk);
                DWORD to=3000;
                setsockopt(newSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
                if (ws_handshake(newSock)) {
                    u_long nonblk=1; ioctlsocket(newSock, FIONBIO, &nonblk);
                    bool added = false;
                    for (int i=0; i<MAX_CLIENTS; i++) {
                        if (clients[i].sock == INVALID_SOCKET) {
                            clients[i].sock=newSock; clients[i].nQueries=0; clients[i].subscribed=false;
                            if (i >= nClients) nClients = i+1;
                            added = true;
                            char snap[4096]={};
                            BuildLegacyJson(snap, sizeof(snap));
                            ws_send(newSock, snap, (int)strlen(snap));
                            break;
                        }
                    }
                    if (!added) closesocket(newSock);
                } else {
                    closesocket(newSock);
                }
            }

            for (int i=0; i<nClients; i++) {
                if (clients[i].sock == INVALID_SOCKET) continue;
                char msg[1024]={};
                int r = ws_recv_msg(clients[i].sock, msg, sizeof(msg));
                if (r == -1) {
                    closesocket(clients[i].sock);
                    clients[i].sock=INVALID_SOCKET; clients[i].subscribed=false; clients[i].nQueries=0;
                } else if (r == 1) {
                    if (parse_subscribe(msg, clients[i]))
                        clients[i].subscribed = true;
                    else {
                        // Bad token or no allowed verbs — disconnect
                        closesocket(clients[i].sock);
                        clients[i].sock=INVALID_SOCKET; clients[i].subscribed=false; clients[i].nQueries=0;
                    }
                }
            }

            Sleep(10);
        }

        for (int i=0; i<nClients; i++)
            if (clients[i].sock != INVALID_SOCKET) closesocket(clients[i].sock);
        closesocket(srv);
        WSACleanup();
    }

    static DWORD WINAPI ThreadProc(LPVOID p)
    {
        ((NowPlayingPlugin*)p)->ServerLoop();
        return 0;
    }
};

// ─── DLL entry point ──────────────────────────────────────────────────────────
HRESULT VDJ_API DllGetClassObject(const GUID &rclsid, const GUID &riid, void **ppObject)
{
    if (memcmp(&rclsid,&CLSID_VdjPlugin8,   sizeof(GUID))==0 &&
        memcmp(&riid,  &IID_IVdjPluginBasic8,sizeof(GUID))==0)
    {
        *ppObject = new NowPlayingPlugin();
        return NO_ERROR;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
