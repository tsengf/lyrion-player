// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace tray::help {
inline constexpr char playerName[] =
    "Name shown in Lyrion and Settings. Names can repeat; the Player ID identifies this instance.";
inline constexpr char server[] =
    "Select Automatic to use the first replying Lyrion server, choose a discovered server, "
    "or select Manual address. The server list refreshes automatically.";
inline constexpr char host[] =
    "Hostname or IPv4 address of your Lyrion server. Editable for Manual address only. "
    "Automatic shows the discovered address without fixing the player to that server. "
    "Do not include a URL or port.";
inline constexpr char port[] =
    "SlimProto connection port, normally 3483. This is separate from Lyrion's web "
    "interface port, normally 9000.";
inline constexpr char playerId[] =
    "Unique MAC-style identifier generated for this player. Keep it unchanged to preserve "
    "Lyrion's player settings. It does not change your network adapter's address.";
inline constexpr char webUrl[] =
    "Optional http:// or https:// address for Open Lyrion. Leave blank to use the server's "
    "discovered web port, or 9000 by default.";
inline constexpr char startWithApp[] =
    "Start this player whenever the tray app opens. To open the app automatically when "
    "you sign in, enable Start at login in the tray menu.";
}
