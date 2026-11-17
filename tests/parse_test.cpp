#include <cstdio>
#include <cstring>
#include <string>

#include "core/api.h"
#include "core/tl.h"

using namespace tgnx;

namespace {

void writeEmptyVector(TlWriter& w) {
    w.u32(0x1cb5c415);
    w.u32(0);
}

void writePeerUser(TlWriter& w, int64_t id) {
    w.u32(0x59511722);  // peerUser
    w.i64(id);
}

void writeUser(TlWriter& w, int64_t id, const std::string& first) {
    w.u32(0xb1b8cc83);      // user
    w.u32(1u << 1);         // first_name
    w.u32(0);               // flags2
    w.i64(id);
    w.str(first);
}

void writeMessage(TlWriter& w, int64_t peerId, const std::string& text) {
    w.u32(0x7600b9d3);  // message
    w.u32(0);           // flags
    w.u32(0);           // flags2
    w.i32(7);           // id
    writePeerUser(w, peerId);
    w.i32(1700000000);  // date
    w.str(text);
}

Bytes buildDialogs() {
    TlWriter w;
    w.u32(0x15ba6c40);  // messages.dialogs
    w.u32(0x1cb5c415);
    w.u32(1);
    w.u32(0xfc89f7f3);  // dialog
    w.u32(0);           // flags
    writePeerUser(w, 42);
    w.i32(7);  // top_message
    w.i32(0);  // read_inbox_max_id
    w.i32(0);  // read_outbox_max_id
    w.i32(2);  // unread_count
    w.i32(0);  // unread_mentions_count
    w.i32(0);  // unread_reactions_count
    w.i32(0);  // unread_poll_votes_count
    w.u32(0x99622c0c);  // peerNotifySettings
    w.u32(0);           // flags
    w.u32(0x1cb5c415);
    w.u32(1);
    writeMessage(w, 42, "Привет");
    writeEmptyVector(w);  // chats
    w.u32(0x1cb5c415);
    w.u32(1);
    writeUser(w, 42, "Аня");
    return w.take();
}

Bytes buildHistory() {
    TlWriter w;
    w.u32(0x1d73e7ea);  // messages.messages
    w.u32(0x1cb5c415);
    w.u32(1);
    writeMessage(w, 42, "Привет");
    writeEmptyVector(w);  // topics
    writeEmptyVector(w);  // chats
    writeEmptyVector(w);  // users
    return w.take();
}

}  // namespace

int main() {
    auto dialogs = api::parseDialogs(buildDialogs());
    if (dialogs.size() != 1) {
        std::printf("dialog count %zu\n", dialogs.size());
        return 1;
    }
    if (dialogs[0].title != "Аня" || dialogs[0].preview != "Привет" || dialogs[0].unread != 2 ||
        dialogs[0].peer.id != 42) {
        std::printf("bad dialog: title=%s preview=%s unread=%d peer=%lld\n",
                    dialogs[0].title.c_str(), dialogs[0].preview.c_str(), dialogs[0].unread,
                    (long long)dialogs[0].peer.id);
        return 1;
    }
    auto messages = api::parseHistory(buildHistory());
    if (messages.size() != 1 || messages[0].text != "Привет" || messages[0].id != 7) {
        std::printf("bad history\n");
        return 1;
    }
    std::printf("parse tests passed\n");
    return 0;
}
