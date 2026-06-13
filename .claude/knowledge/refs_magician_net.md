# Refs mine: SacredMagician + SacredAncariaConnection + SacredGameTools

Source of truth for **Sacred multiplayer network protocol**, the **TCP lobby/game packet
format + CRC32**, the **server-announce UDP packet**, the **balance.bin layout**, and the
**hero (.sav) section/compression format**. Extracted to `E:/refs_extract/<name>`.

Game EXE base 0x00400000, no ASLR (file offset = VA - 0x400000). All multi-byte fields below
are little-endian unless stated. This file is read-only research — nothing here was built/run.

---

## 1. Repo purposes / architecture

| Repo | Lang | Purpose | High-value content |
|---|---|---|---|
| **SacredAncariaConnection** (`-main`) | C# (.NET WinForms client + ASP.NET Core server) | Modern (2021) matchmaking replacement so Sacred can play online without VPN. Client is a **local UDP↔HTTP bridge**; server is a REST server list. | **Server-announce UDP packet format**, zlib framing, REST matchmaking API, server-flag bitfield decode. |
| **SacredGameTools** (`-main`, "SacredFilter" subproject) | C++ (MFC, ~2005, by SonicMouse / Andrew Heinlein) | A man-in-the-middle **lobby proxy** ("Sacred Filter") that sits between `sacred.exe` and Ascaron's real lobby server, rewriting server entries to route games through localhost. | **TCP lobby/game packet header layout**, **TINCAT2.DLL CRC32 (table+asm)**, SERVERPACKET struct, lobby packet-type IDs, full proxy state machine. |
| **SacredGameTools** (other subprojects) | C++ MFC | Hero/item editors, CD-key registry fix, raw hex editor, level unlock, gold cheat. | **shlib** = hero-save (.sav) section format + zlib; balance/registry knowledge. |
| **SacredMagician** (`-master`) | Kotlin / JavaFX (JVM) | GUI **balance.bin editor** for Sacred + Underworld (v1.0–2.29.14). | **Concrete balance.bin field offsets** (int vs float), LE read/write recipe. |

Author note: SacredFilter and shlib are by **Andrew Heinlein ("SonicMouse")** — same author cited
across the broader refs corpus; treat his offsets/algorithms as high-confidence, reverse-engineered
directly from TINCAT2.DLL v2.0.24.0 and sacred.exe.

---

## 2. NETWORK PROTOCOL — the core deliverable

Sacred's online stack has **two transports**:
- **UDP**: server *announce/broadcast* (a host advertises a running game). LAN broadcast + (via SAC) relayed to a web server list.
- **TCP**: the *lobby* protocol (client↔Ascaron lobby `sacredenu.ascaron-net.com:7066`) and *game* connections. Uses the TINCAT2 networking middleware (CRC32-checksummed framed packets).

### 2.1 TCP lobby/game packet frame  (from SacredFilter, validated against TINCAT2.DLL)

```
[ HEADER  : 28 bytes ] [ BODY : variable ]
```
- `HEADER_SIZE = 28`.
- **Body length** is read from header at **offset +20** as `unsigned long` (`#define BODYSIZE(p) (*(unsigned long*)&p[20])`).
- **Header +12** (`unsigned long`) = **packet format**. Format `== 5` is an "oddball" raw/passthrough packet (proxy forwards body verbatim, no parsing).
- **CRC32 of the BODY** is stored in the **last 4 bytes of the header** = offset **+24** (`HEADER_SIZE - sizeof(ulong)`). Sender recomputes it over the body before send:
  `*(ulong*)&header[24] = CRC32(body, bodyLen);`
- Receive loop = "read 28-byte header, then read `BODYSIZE` bytes" (classic length-prefixed framing). See `CSacredSocket::OnReceive`.

Header byte map (what's known):
| Off | Size | Meaning |
|---|---|---|
| +0  | 12 | (unknown / routing — passed through) |
| +12 | 4  | packet **format** (5 = raw passthrough) |
| +16 | 4  | (unknown) |
| +20 | 4  | **body length** |
| +24 | 4  | **CRC32(body)** |

Body byte map (lobby control packets):
| Off | Size | Meaning |
|---|---|---|
| +2 | 2 (`u16`) | **packet type** (see IDs below) |

**Lobby packet type IDs** (`usPacketType = *(u16*)&body[2]`):
- `12`, `13`, `21` = **server add/update** (a game server appeared/changed in the list).
- `14` = **server remove**.
- `56` = **welcome / MOTD** message (commented-out hook rewrites a string at `body[150]` and writes its length to `*(u32*)&body[20]` — i.e. lobby welcome text is at body+150 with a u32 length at body+20).

When the proxy sees a `12/13/14/21`, it parses the body as a **SERVERPACKET** (below) and rewrites
the IP/port to localhost so the game connects through the proxy.

### 2.2 SERVERPACKET (lobby "server in list" body)  — `#pragma pack(1)`, 18+80+4+4+2+10+4+12 = 134 bytes

```c
typedef struct {
    unsigned char  header[18];   // packet header within body
    char           name[80];     // server/game name (ASCII, null-terminated)
    unsigned long  ip1;          // "internal" IP (often LAN)
    unsigned long  ip2;          // "external"/public IP   <-- the routable one
    unsigned short port;         // game server TCP port
    unsigned char  unknown[10];
    unsigned long  id;           // lobby-assigned server ID (key for tracking)
    unsigned char  unknown2[12];
} SERVERPACKET;
```
IPs are raw 4-byte network-order (`inet_ntoa(*(in_addr*)&ip2)`). To hijack routing the proxy sets
`ip1 = ip2 = inet_addr("127.0.0.1")` and `port = <local proxy port>`.

### 2.3 Static endpoints / ports (SacredFilter)
- Real lobby: **`sacredenu.ascaron-net.com` : 7066** (`SACRED_NETWORK_LOBBYPORT`).
- Proxy's fake local lobby listen port: **13763** (`VIRTUAL_NETWORK_LOBBYPORT`).
- Proxy's per-game local listen ports start at **15763** (`VIRTUAL_NETWORK_GAMEPORT_BASE`) and increment, probing for a free port.

### 2.4 Server-announce UDP packet  (SacredAncariaConnection — the *modern* path, 2021)

This is the packet a **running game host broadcasts** to advertise itself (and what SAC relays to
its web server list). Frame:
```
[ 4-byte header (kept verbatim) ] [ zlib-compressed payload ]
```
- **Compression**: payload is **zlib** (Ionic.Zlib `ZlibStream`, i.e. RFC-1950 zlib, not raw deflate, not gzip). Decompress = strip first 4 bytes, inflate the rest. Recompress = `ZlibStream.CompressBuffer(payload)` then re-prepend the original 4-byte header.
- The 4-byte header is copied unchanged from in→out; only the inflated body is parsed/edited.

**Uncompressed (inflated) server-info payload byte map** (from `Server.ServerFromUncompressedData`):
| Off | Size | Field | Decode |
|---|---|---|---|
| +2..+3 | 2 | **Port** | `port = body[3]*256 + body[2]` (LE u16) |
| +4..+7 | 4 | **IP** | stored reversed: `Ip = {body[7],body[6],body[5],body[4]}` (i.e. body holds it little-endian / byte-swapped) |
| +8 | 1 | **flags** | bit `0x04` = Locked; bit `0x02` = Pass(word) required; bit `0x80` (`>>7`) = Started; bits `(b>>4)&7` = **GameMode** |
| +9 | 1 | **Difficulty** | enum below |
| +12 | 1 | CurNumber | current players |
| +13 | 1 | MaxNumber | max players |
| +14..+61 | 48 | **Name** | **UTF-16LE** (`UnicodeEncoding`), null-trimmed, 24 chars max |

When relaying, SAC **overwrites the IP** in the inflated body at +4..+7 with the host's real public
IP (byte-reversed: `body[4]=ip[3] … body[7]=ip[0]`) before re-compressing — because the host can't
know its own NAT'd public IP.

**Difficulty enum** (byte +9): `0=Bronze, 1=Silver, 2=Gold, 4=Platinum, 8=Niobium` (bit-flag style, NOT 0..4).
**GameMode enum** (`(flags>>4)&7`): `0=Underworld Campaign, 1=Campaign, 2=Free, 4=Playerkiller`.

UDP ports (SAC defaults, mirror `Gameserver.cfg`/`Settings.cfg`):
- `ServerPort = 2006` → `NETWORK_PORT_LISTEN` (SAC listens here for the game's announce).
- `ClientPort = 2005` → `NETWORK_PORT_BROADCAST` (SAC re-emits to the Sacred client on loopback / LAN broadcast).
- SAC re-sends collected servers to the local Sacred client via UDP to `127.0.0.1:2005` on a ~1s loop.

### 2.5 SAC matchmaking REST API (server side, ASP.NET Core)
Replaces the dead Ascaron master server. Client `SACServerCommunication` talks JSON over HTTP:
- `GET  /api/servers/{version}` → `ServerListResponse { Servers[], Motd, UpdateMessage, ToUpdate, YourIp }`. If `{version}` != latest, returns `ToUpdate=true` + update message (gates old clients). `YourIp` = `RemoteIpAddress` — how the client learns its own public IP.
- `POST /api/servers` (body: `Server[] {Name, Port, PacketBase64, IpAddess}`) → `MyServerStatus[] {Port, PortState, NameAlreadyUsed}`.
- `POST /api/servers/delete` (body: `Server[]`) → 200.
- `Server` is keyed server-side by **(ip, port)**. Names must be unique. Entries **expire after 15000 ms** without a refresh. **Port reachability** is verified by the server opening a `TcpClient.Connect(ip, port)` → `Reachable`/`Unreachable`; only `Reachable` servers are listed. Responses are `no-cache`.
- `PacketBase64` = base64 of the **raw compressed UDP announce packet** — the server stores/relays it opaquely; the client decodes it back into a `Server` (`ServerFromBase64`). This is the bridge between the binary UDP world and the JSON web world.

Client refresh loop (`SACServerPacketManager.LoopAsync`): post my servers + fetch list every **7000 ms**, drop my own servers not re-announced within **15000 ms**.

---

## 3. balance.bin format (SacredMagician)

`balance.bin` is a flat binary blob read/written by **absolute byte offset**, **little-endian**.
Two field kinds: 4-byte **int** and 4-byte **float**. (Magician reads 8 bytes then takes the
first int/float via `ByteBuffer.LITTLE_ENDIAN`.) Concrete known offsets:

**Player skill-unlock levels** (int): 1st=780, 2nd=784, 3rd=788, 4th=792, 5th=796, 6th=800.

**Difficulty min/max level gates** (int): silverMin=2488, goldMin=2492, platinumMin=2496, niobiumMin=2500, platinumMax=2504.

**Per-difficulty multipliers** (FLOAT). Layout is interleaved by tier; base offsets:
| Stat | Silver | Bronze | Gold | Platinum | Niobium | Global/Server |
|---|---|---|---|---|---|---|
| AW/VW (attack/defense) | 1812 | 1832 | 1816 | 1820 | 1824 | 1828 |
| HitPoints | 1836 | 1856 | 1840 | 1844 | 1848 | 1852 |
| Damage | 1860 | 1880 | 1864 | 1868 | 1872 | 1876 |
| Resistance | 1884 | 1904 | 1888 | 1892 | 1896 | 1900 |
| Experience | 1908 | 1928 | 1912 | 1916 | 1920 | 1924 (server) |

(Note the irregular pairing: Bronze sits +20 above Silver in each row; Gold/Plat/Nio/Global step +4 from Silver.)

**Region monster-quantity (int), per region** at: SouthCenter=22392, NorthCenter=22456, Swamp=22520,
West=22584, North=22648, Lava=22712, Shaddar=22776, UpperUnderworld=22840, Lower=22904 (stride 64 bytes).

Magician writes via `RandomAccessFile.seek(offset); write(4 LE bytes)` — in-place patch, no
checksum/structure rewrite needed. Useful sample file: `SacredMagician-master/SacredMagician/src/etc/balance.bin`.

---

## 4. Hero save (.sav) format (SacredGameTools/shlib)

`shlib.dll` (SonicMouse) decompiles/recompiles Sacred hero files. Structure:
- File magic (`u32` at start): **`0x07484D41`** ("AMH\x07") = Sacred v1.x heroes; **`0x1B484D41`** ("AMH\x1B") = Sacred 2.x (Underworld) heroes. (`HERO_HEADER7` / `HERO_HEADER27`.)
- A fixed **0xF8-byte "fluff" header** precedes the section table.
- Body = array of **sections**, each described by:
  ```c
  struct SECTION_DESC { u32 type; u32 offset; u32 sizeinflated; }  // pack(1)
  ```
- A section whose stored marker equals **`0xBAADC0DE`** (`COMPRESSED_CODE`) is **zlib-compressed** (inflate to `sizeinflated`); otherwise stored raw.
- Exposed C API (`shlib.def`): `shlib_CreateHero/DestroyHero/CompileHero/DecompileHero/GetSectionsList/GetHeroSection/SetHeroSection/Compress/Uncompress`. Section-type IDs are addressed by `u32` keys (not enumerated in headers seen — discover empirically per section).

This is the editable container for inventory/items; item-level editing lives in
`SacredItemManager`/`SacredItemEdit` (`Hero.cpp`, `Item.cpp`) if item byte layout is later needed.

---

## 5. CRC32 (TINCAT2.DLL) — exact reproduction

Sacred's TCP checksum = **standard CRC-32 (zlib/IEEE polynomial 0xEDB88320), table-driven, init 0,
no final XOR, no input/output reflection beyond the standard reflected table**. Confirmed identical
to the canonical zlib table. SonicMouse ripped it from **TINCAT2.DLL v2.0.24.0**:
- 256-entry table @ file offset **0x0004986C**.
- Routine @ offset **0x0000DD30**.

Reference C (portable, equivalent to the asm in `SacredCRC32.h`):
```c
uint32_t sacred_crc32(const uint8_t* buf, uint32_t len){
    uint32_t crc = 0;                    // init 0 (NOT 0xFFFFFFFF)
    for(uint32_t i=0;i<len;i++)
        crc = (crc >> 8) ^ table[(crc ^ buf[i]) & 0xFF];
    return crc;                          // no final XOR
}
```
Full 256-entry table is in `E:/refs_extract/SacredGameTools/SacredGameTools-main/SacredFilter/SacredCRC32.h`
(it is the textbook zlib table — can be regenerated from polynomial 0xEDB88320).

---

## 6. Reusable knowledge table

| Knowledge | Where (ref path) | Use |
|---|---|---|
| TCP lobby frame (28B hdr, body-len@+20, CRC@+24, fmt@+12) | SacredFilter/SacredFilter.h, SacredSocket.cpp | Sniff/inject lobby traffic; reimplement a master server |
| Lobby packet type IDs (12/13/14/21/56) | SacredFilter/LobbyManager.cpp | Identify server add/remove/MOTD packets |
| SERVERPACKET 134-byte struct | SacredFilter/SacredFilter.h | Parse/rewrite lobby server entries |
| Sacred TINCAT2 CRC32 (init 0, no final XOR, poly 0xEDB88320) | SacredFilter/SacredCRC32.h | Validate/forge any TCP packet |
| Real lobby host+port `sacredenu.ascaron-net.com:7066` | SacredFilter/SacredFilter.h | Redirect/replace dead master server |
| UDP announce: 4B hdr + zlib body | SAC/UdpPacketManager.cs, Utils.cs | Emit/parse server broadcasts |
| UDP body byte map (port@2, ip@4 reversed, flags@8, diff@9, cur@12,max@13, name UTF-16@14) | SAC/Models/Server.cs | Decode/build server-info packets |
| Difficulty (1=Si,2=Go,4=Pt,8=Nb) & GameMode ((f>>4)&7) enums | SAC/Models/Server.cs | Display/filter games |
| SAC REST master API (GET/POST /api/servers, 15s expiry, TCP port check) | SAC server Controllers/Services | Stand up a modern matchmaking server |
| SAC default ports 2006 listen / 2005 broadcast | SAC/Models/Context.cs | Wire LAN/loopback bridge |
| balance.bin offset table (int+float, LE) | SacredMagician LoadBalanceBinData.kt / SaveBalanceBinData.kt | Read/patch difficulty + skill + region tuning |
| Hero .sav: magic 0x07/0x1B484D41, 0xF8 hdr, SECTION_DESC, 0xBAADC0DE=zlib | shlib/Hero.h, ZlibWrapper.h | Read/write hero saves & items |
| CDKey-bug fix: delete `HKCU\Software\Ascaron Entertainment\Sacred` keys | SacredCDKeyFix/app.cpp | Diagnose install/registry issues |

---

## 7. C++-port candidates  (for the SDK DLL / future multiplayer modding)

- **TODO(port): Sacred TINCAT2 CRC32** — trivially portable, dependency-free. Drop the `SacredCRC32.h` table + the portable loop in §5 into the SDK as `sacred_crc32(buf,len)`. Prerequisite for *any* packet forging/validation. (Source: SacredFilter/SacredCRC32.h — already C++.)
- **TODO(port): TCP lobby packet framer/parser** — port `CSacredSocket::OnReceive` framing (read 28B header → read `*(u32*)&hdr[20]` body bytes) + `SendPacket` (recompute CRC into hdr[24]) into a transport-agnostic `LobbyPacket` codec. Strip MFC `CSocket`; back it with raw winsock or the SDK's existing IO. Enables a man-in-the-middle/observer for live lobby modding.
- **TODO(port): SERVERPACKET + lobby-packet-type dispatch** — port the struct (§2.2) and the `12/13/14/21/56` switch from `LobbyManager.cpp` to classify and (optionally) rewrite server-list packets in-engine. Foundation for a private/community master server.
- **TODO(port): UDP server-announce codec** — reimplement `ServerFromUncompressedData` / `CompressData` (§2.4) in C++ (zlib `uncompress`/`compress` + the byte map). Lets the SDK both *advertise* a modded host and *enumerate* games without the C# SAC bridge. Pair with zlib (already a dependency via shlib).
- **TODO(port): hero .sav section (de)compiler** — `shlib` is already C++ (`CHero` + `CZlibWrapper`); lift `DecompileHero`/`CompileHero` + the 0xBAADC0DE/zlib + SECTION_DESC handling into the SDK to read/patch hero inventories at runtime or offline. Watch the two magics (v1 0x07.. vs Underworld 0x1B..).
- **TODO(port): balance.bin accessor** — tiny: a `GetFloat/SetFloat/GetInt/SetInt(offset)` over the file plus the §3 offset constants. Lets the SDK live-tune difficulty/XP/skill gates. (Currently only exists as Kotlin in SacredMagician.)
- **TODO(port, optional): SAC-style REST master server** — if reviving online play, the ASP.NET Core `ServersList` design (concurrent dict keyed (ip,port), 15s TTL, async TCP reachability probe, version-gating, `YourIp` echo) is a clean blueprint to reimplement in any stack; not C++ per se but the *protocol contract* is the reusable part.

### Not ported elsewhere — confirmed unique
SacredGameTools' **SacredFilter** (lobby proxy) and **shlib** (hero codec) C++ sources, and the
**SAC UDP/REST protocol**, are NOT duplicated by the simpler tools in the refs corpus (HeroDump,
PakExtractor, etc.). Recommend keeping this mine. SacredMagician's *only* durable value is the
balance.bin offset table (§3); its Kotlin/JavaFX UI is not reusable for a C++ DLL.

### Archives in refs that may deepen this topic (not mined here)
`LadderClient.zip`, `ServerChat.zip`, `sacclient11.zip`, `SVAccountManager.zip`, `LadderClient`,
`suac_recoded_127.zip`, `Inoff_Sacred_Patch_Source.zip` — likely contain more
lobby/account/ladder protocol detail if §2 needs extending.
