// SPDX-FileCopyrightText: 2023 James Graham <james.h.graham@protonmail.com>
// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
//
// stateSentence() is NeoChat's src/libneochat/eventhandler.cpp
// (EventHandler::genericBody) reworded: the cases it distinguishes, and which
// ones are worth distinguishing at all, are its answer rather than this
// project's. The classification is done before the string is reached here, so
// the switch is flat where theirs nests, and the strings carry i18nc contexts
// this tree requires and NeoChat's do not.
#include "MatrixTranslate.h"

#include <KLocalizedString>

namespace koutnet::matrix
{

double secondsFromMs(qint64 ms)
{
    if (ms <= 0)
        return 0.0;
    return double(ms) / 1000.0;
}

QString mediaLabel(MediaKind kind, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
        return trimmed;

    switch (kind) {
    case MediaKind::Image:
        return i18nc("@label an attached picture that arrived with no file name", "Picture");
    case MediaKind::Video:
        return i18nc("@label an attached video that arrived with no file name", "Video");
    case MediaKind::Audio:
        return i18nc("@label an attached sound recording that arrived with no file name", "Audio");
    case MediaKind::File:
        return i18nc("@label an attached file that arrived with no file name", "File");
    case MediaKind::None:
        break;
    }
    return QString();
}

QString stateSentence(StateChange change, const QString &actor, const QString &subject)
{
    // Never blank: a state line whose actor could not be resolved still has to
    // read as a sentence rather than start with a space.
    const QString who = actor.trimmed().isEmpty() ? i18nc("@info in-timeline notice, a room member with no resolvable name", "Someone") : actor;
    const QString whom = subject.trimmed().isEmpty() ? i18nc("@info in-timeline notice, a room member with no resolvable name", "someone") : subject;

    switch (change) {
    case StateChange::None:
        return QString();
    case StateChange::Joined:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 joined the room.", who);
    case StateChange::Left:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 left the room.", who);
    case StateChange::Invited:
        return i18nc("@info in-timeline notice, %1 invited %2", "%1 invited %2.", who, whom);
    case StateChange::InviteWithdrawn:
        return i18nc("@info in-timeline notice, %1 took back an invitation to %2", "%1 withdrew the invitation to %2.", who, whom);
    case StateChange::InviteRejected:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 declined the invitation.", who);
    case StateChange::Kicked:
        return i18nc("@info in-timeline notice, %1 removed %2 from the room", "%1 removed %2 from the room.", who, whom);
    case StateChange::Banned:
        return i18nc("@info in-timeline notice, %1 banned %2", "%1 banned %2 from the room.", who, whom);
    case StateChange::SelfBanned:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 banned themselves from the room.", who);
    case StateChange::Unbanned:
        return i18nc("@info in-timeline notice, %1 lifted a ban on %2", "%1 unbanned %2.", who, whom);
    case StateChange::SelfUnbanned:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 lifted their own ban.", who);
    case StateChange::KnockRequested:
        return i18nc("@info in-timeline notice, %1 asked to be let into the room", "%1 asked for an invitation.", who);
    case StateChange::DisplayNameSet:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 set a display name for this room.", who);
    case StateChange::DisplayNameChanged:
        return i18nc("@info in-timeline notice, %1 is the new name, %2 the old one", "%1 changed their display name from %2.", who, whom);
    case StateChange::DisplayNameCleared:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 cleared their display name.", who);
    case StateChange::MemberAvatarChanged:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 changed their picture.", who);
    case StateChange::RoomCreated:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 created the room.", who);
    case StateChange::RoomUpgraded:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 upgraded the room to a newer version.", who);
    case StateChange::RoomNameSet:
        return i18nc("@info in-timeline notice, %1 is a room member, %2 the new room name", "%1 named the room %2.", who, subject);
    case StateChange::RoomNameCleared:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 cleared the room name.", who);
    case StateChange::TopicSet:
        return i18nc("@info in-timeline notice, %1 is a room member, %2 the new topic", "%1 changed the topic to: %2", who, subject);
    case StateChange::TopicCleared:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 cleared the topic.", who);
    case StateChange::AliasSet:
        return i18nc("@info in-timeline notice, %1 is a room member, %2 the new room address", "%1 set the room address to %2.", who, subject);
    case StateChange::AliasCleared:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 cleared the room address.", who);
    case StateChange::RoomAvatarChanged:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 changed the room picture.", who);
    case StateChange::EncryptionEnabled:
        return i18nc("@info in-timeline notice, %1 is a room member", "%1 turned on end-to-end encryption.", who);
    case StateChange::PowerLevelsChanged:
        return i18nc("@info in-timeline notice, 'permissions' means Matrix power levels, %1 is a room member",
                     "%1 changed who is allowed to do what here.",
                     who);
    case StateChange::Unknown:
        break;
    }
    return i18nc("@info in-timeline notice, a room state change this build has no words for", "%1 changed something about the room.", who);
}

Row rowFor(const RawEvent &event)
{
    Row row;
    // Without an id there is no duplicate check, and sync replays the tail of a
    // timeline on every reconnect.
    if (event.eventId.isEmpty())
        return row;

    row.msgId = event.eventId;
    row.ts = secondsFromMs(event.originTimestampMs);
    row.isOwn = event.isOwn;
    row.sender = event.senderName.isEmpty() ? event.senderId : event.senderName;

    // State first: a redacted state event still happened, and hiding it would
    // leave a join or a topic change out of the record because somebody later
    // deleted the event.
    if (event.state != StateChange::None) {
        row.kind = RowKind::System;
        row.text = stateSentence(event.state, row.sender, event.stateSubject);
        return row;
    }

    // A redacted message keeps its place in the timeline, but its content is
    // gone. Said out loud rather than dropped: a message that vanishes without
    // a trace reads as one that was never sent.
    if (event.redacted) {
        row.kind = RowKind::System;
        row.text = i18nc("@info in-timeline notice, %1 is who sent the message that was withdrawn", "%1 deleted a message.", row.sender);
        return row;
    }

    // An m.room.encrypted that is still an m.room.encrypted by the time it gets
    // here is one libQuotient could not open - it substitutes the decrypted
    // event in place when it can. Almost always a missing megolm key: history
    // from before this device existed, or a sender who has not shared with it.
    // Said in the timeline rather than skipped, because a message that leaves no
    // trace reads as one that was never sent, and never claimed as unreadable
    // for the room as a whole - the rest of it may read perfectly well.
    if (event.encrypted) {
        row.kind = RowKind::Encrypted;
        row.text = i18nc("@info in-timeline notice, an encrypted message that could not be decrypted", "An encrypted message this device has no key for.");
        return row;
    }

    if (event.media != MediaKind::None) {
        row.kind = RowKind::Attachment;
        row.media = event.media;
        row.mediaUrl = event.mediaUrl;
        row.mediaName = mediaLabel(event.media, event.mediaName.isEmpty() ? event.body : event.mediaName);
        row.mediaMime = event.mediaMime;
        row.mediaSize = event.mediaSize;
        row.mediaWidth = event.mediaWidth;
        row.mediaHeight = event.mediaHeight;
        row.mediaDurationMs = event.mediaDurationMs;
        // The name is what the timeline labels the attachment with, so it is
        // also what the row's text is - the same convention the LAN file path
        // already follows.
        row.text = row.mediaName;
        return row;
    }

    if (!event.textLike) {
        // A msgtype with no renderer here - m.location, m.key.verification.request
        // and whatever comes next. Named rather than dropped.
        row.kind = RowKind::System;
        row.text = event.body.isEmpty()
            ? i18nc("@info in-timeline notice, a Matrix message this build cannot render", "A message this version cannot show.")
            : i18nc("@info in-timeline notice, %1 is the description the sender gave", "A message this version cannot show: %1", event.body);
        return row;
    }

    // An m.text with an empty body is legal and says nothing.
    if (event.body.isEmpty())
        return row;

    row.kind = RowKind::Text;
    row.text = event.body;
    return row;
}

QString conversationTitle(const QString &displayName, const QString &roomId)
{
    const QString trimmed = displayName.trimmed();
    return trimmed.isEmpty() ? roomId : trimmed;
}

} // namespace koutnet::matrix
