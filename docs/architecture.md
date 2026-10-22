# Architecture

## Independent player backend

`src/player` implements the player without Squeezelite. The executable uses C++17
with C99 native-audio/discovery adapters. The tray application's configuration,
player identities, launch arguments, and version-1 JSON IPC remain compatible.

`main.cpp` validates the command line, initializes codec libraries and native
audio, and owns shutdown. `managed.cpp` accepts `stop` or owner EOF on stdin and
sets a process-lifetime atomic cancellation flag. All stdout output in managed
mode is versioned JSON; diagnostics go to stderr or the configured log file.

`protocol.cpp` discovers/connects to Lyrion and runs the SlimProto control loop.
Server frames have a 16-bit network-order payload length; player frames have a
four-byte opcode and a 32-bit network-order body length. Fragmented and coalesced
frames are handled incrementally. Length and field validation precedes dispatch;
malformed control frames close the connection and trigger a bounded retry delay.
Unknown opcodes are ignored. A fresh parser is created on every connection.

The handshake uses SqueezePlay device ID 12 and the independent model name
`lyrion-player`. Only implemented formats are advertised. `strm`, `audg`, `aude`,
`cont`, `codc`, `setd` and IPv4 `serv` commands are supported. STAT reports use the
53-byte layout, network byte order, a wrapping monotonic millisecond clock,
rendered-frame playback time, estimated device latency, and echoed timer tokens.
Track start, decoder completion, buffer readiness, underrun, and disconnect events
are distinct. The connection sends periodic status and reconnects on server loss.
Server redirection retains the configured SlimProto port and optional sync-group
capability. Legacy cloud-server redirection and direct local-file access are not
implemented or advertised.

`net.cpp` supplies nonblocking IPv4 TCP connections and bounded, cancellable I/O.
Connect and write deadlines are five seconds; an inactive HTTP read times out
after fifteen seconds. Cancellation is checked at least every 50 ms while waiting
on sockets. Hostnames are resolved before connecting; platform DNS lookup itself
is synchronous.

`http.cpp` runs on a decoder worker, forwards response headers to Lyrion, and
streams HTTP bodies through a 16 KiB read buffer. It handles Content-Length,
connection-close bodies, chunked transfer encoding (including trailers), and ICY
metadata. Headers, individual lines, and trailers have explicit limits. Direct
streams wait for `cont` before reading audio, and unknown formats wait for `codc`.
Redirects, HTTPS, and compressed HTTP content require the server's proxy path.

`codecs.cpp` calls libFLAC, mpg123, and libvorbisfile directly. Its PCM reader
handles raw integer samples, RIFF/WAV, AIFF, and uncompressed AIFC. WAV integer and
32-bit float samples are supported; output is mono-expanded or stereo. Optional
faad2 support handles ADTS/ADIF AAC, not MP4 containers. Other formats and
multichannel audio require server transcoding. Decoder callback errors are caught
inside C callbacks and propagated back to the worker. Soxr HQ converts decoded
samples to 48 kHz signed 32-bit stereo, draining its tail at track completion.

`playback.cpp` owns bounded, per-track output rings (eight seconds each, at most
four queued tracks). The producer waits on a condition variable when a ring is
full; stop/flush cancellation releases it. Track markers survive prefetching:
decoder completion requests the next stream while the current track's buffered
audio continues. The output callback advances track clocks as frames are consumed,
changes the current track at its boundary, applies per-channel volume/replay gain,
channel selection and polarity, and clips before converting back to integer PCM.
Linear fades and crossfades mix already-buffered tracks at the output boundary;
the available buffered overlap limits crossfade duration.
Pause and power-off produce silence without consuming audio. Timed starts,
temporary silence and skip adjustments use the 48 kHz clock. No multiroom
synchronization accuracy guarantee is made.

The control loop alone writes the SlimProto socket. Decoder and output events are
queued under the playback mutex; no socket I/O runs inside the audio callback.
Shutdown cancels and joins the decoder, then joins native audio callbacks before
freeing playback state. The silent test executable exercises the same backend,
changing only the native audio device selection.

## Discovery and native audio

`src/discovery.c` enumerates active IPv4 broadcast interfaces using `getifaddrs`
on macOS or `GetAdaptersAddresses` on Windows. Each LAN probe uses a source-bound
socket and its subnet broadcast. macOS also uses `IP_BOUND_IF`; Windows requests
the adapter using `IP_UNICAST_IF`. Loopback and tunnel adapters are excluded from
the LAN list. A generic socket also probes localhost and limited broadcast on UDP
port 3483. All sockets are monitored together and closed on reply or timeout;
each retry refreshes the interfaces. The first valid reply selects a server.
The tray's collect-all scan also retrieves names, UUIDs and web ports.

`src/native_audio.c` contains the pinned miniaudio implementation. A worker owns
initialization, start, recovery and teardown. macOS selects Core Audio and Windows
selects WASAPI, always in shared mode using the system default output. There is no
fallback to another audio API or silent production backend. Missing devices retry
every two seconds without consuming buffered audio or dropping the protocol link.

Notifications only set atomic flags; device reinitialization happens on the
worker. macOS uses miniaudio's default-device rerouting. On Windows the worker
queries the native default endpoint ID every 100 ms and recreates the device when
it changes. Miniaudio's automatic WASAPI rerouting is disabled to avoid blocking
inside OS notification callbacks. Device latency is an estimate based on native
period size/count. Physical rerouting and synchronization require hardware checks.

## Qt tray application

`src/tray/qt_main.cpp` owns the Qt application; `qt_app.cpp` provides the
shared Qt menu and About dialog. Windows uses `QSystemTrayIcon`; macOS uses
a small AppKit status-item adapter in `qt_tray_mac.mm` to host the same native
`QMenu` on right click; left click opens Settings. The adapter inspects only
the event type and also accepts non-mouse activation safely. This avoids Qt Cocoa tray activation querying mouse-only properties
when macOS starts menu tracking with a non-mouse event. Tray actions use
`NoRole` so Settings, About and Quit stay in the tray menu instead of merging
into the application menu. `qt_settings.cpp` and `qt_editor.cpp`
provide the shared Qt Widgets Settings window and player editor. The interface
uses the C++17 manager in `core.cpp`; the playback backend has no Qt dependency.
Native helpers in `platform_mac.mm` and `platform_windows.cpp` retain process
ownership, filesystem paths and sign-in registration. JSON configuration has
an explicit schema version and is replaced atomically. Player IDs are generated
once and persisted; duplicate IDs are rejected before launching.

The tray app alone registers for user sign-in. macOS uses `SMAppService`; Windows
stores one `LyrionPlayerTray` value in the current user's Run registry key. Each
configured instance is an ordinary child with redirected stdin and stdout and an
independent log. `--managed` emits versioned newline-delimited JSON
connection/audio events and reads `stop` from stdin. EOF requests shutdown if its
owner disappears. macOS uses posix_spawn and close-on-exec descriptors; Windows
restricts inherited handles and uses one kill-on-close Job Object per player.
The manager stops children cooperatively, then kills any still alive after five
seconds. Settings windows do not own playback lifetime; Quit does.

A stopped instance stays stopped; editing an active one restarts just that child.
Unexpected exits retry at 1/2/4 seconds, with the fourth exit in a minute requiring
manual retry. A launch error affects only that instance. Shutdown disables retries.
A Qt timer polls status every 250 ms without waiting for a server scan: metadata
scans run asynchronously and support cancellation. Scanning starts with the app
and repeats 15 seconds after each scan completes, without overlapping scans. The
shared discovery adapter
has separate first-response and collect-all APIs. NAME/UUID/JSON fields identify
servers and their web ports, with deduplication across interfaces in the manager.

The tray app never creates per-instance startup jobs. Start app at login registers
only the tray application. Old per-player startup registrations from previous
versions must be removed manually before configuring those players in Settings.

A native process-owned lock prevents multiple tray instances, including an older
native frontend running alongside this version. A user-restricted `QLocalServer`
accepts activation requests so launching the app again opens Settings. Saved
configuration, player IDs, bundle ID and login registration names remain unchanged.
Qt windows close without destroying the player manager. Application quit requests
stop all children through the same bounded shutdown path before Qt exits.
