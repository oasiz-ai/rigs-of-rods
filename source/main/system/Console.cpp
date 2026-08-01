/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2019 Petr Ohlidal

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Console.h"

#include "Application.h"
#include "Utils.h"

#include <Ogre.h>

using namespace RoR;
using namespace Ogre;

Console::~Console() noexcept
{
    // OGRE keeps raw LogListener pointers. The explicit renderer guard now
    // destroys Root while this process-wide console is alive; detach here as
    // well so the externally-owned LogManager cannot retain a dead listener
    // after static destruction begins.
    try
    {
        Ogre::LogManager* log_manager = Ogre::LogManager::getSingletonPtr();
        if (log_manager == nullptr)
        {
            return;
        }

        Ogre::Log* default_log = log_manager->getDefaultLog();
        if (default_log == nullptr)
        {
            return;
        }

        default_log->removeListener(this);
        default_log->logMessage(
            "[RoR|Shutdown] Console log listener detached");
    }
    catch (...)
    {
        // A process-lifetime destructor must not throw. The native renderer
        // smoke requires the marker, so a failed detach remains fail-closed.
    }
}

void Console::messageLogged(const Ogre::String& message, Ogre::LogMessageLevel lml, bool maskDebug, const Ogre::String& logName, bool& skipThisMessage)
{
    if (App::diag_log_console_echo->getBool())
    {
        this->forwardLogMessage(CONSOLE_MSGTYPE_LOG, message, lml);
    }
}

void Console::forwardLogMessage(MessageArea area, std::string const& message, Ogre::LogMessageLevel lml)
{
    switch (lml)
    {
    case Ogre::LML_WARNING:
        this->putMessage(area, CONSOLE_SYSTEM_WARNING, SanitizeUtf8String(message));
        break;

    case Ogre::LML_CRITICAL:
        this->putMessage(area, CONSOLE_SYSTEM_ERROR, SanitizeUtf8String(message));
        break;

    default: // LML_NORMAL, LML_TRIVIAL
        this->putMessage(area, CONSOLE_SYSTEM_NOTICE, SanitizeUtf8String(message));
        break;
    }
}

void Console::purgeNetChatMessagesByUser(int user_id)
{
    std::lock_guard<std::mutex> lock(m_messages_mutex); // Scoped lock
    m_messages.erase(std::remove_if(m_messages.begin(), m_messages.end(), [user_id](const Console::Message& msg) { return msg.cm_net_userid == user_id; }), m_messages.end());
}

void Console::handleMessage(MessageArea area, MessageType type, std::string const& msg, int net_userid/* = 0*/, std::string icon)
{
    if (net_userid < 0) // 0=server, positive=clients, negative=invalid
    {
        net_userid = 0;
    }

    // Log message to file
    if (area != MessageArea::CONSOLE_MSGTYPE_LOG && // Don't duplicate echoed log messages
        type != MessageType::CONSOLE_SYSTEM_NETCHAT) // Privacy
    {
        Str<2000> txt;
        txt << "[RoR|";
        switch (area)
        {
            case MessageArea::CONSOLE_MSGTYPE_INFO:   txt << "General"; break;
            case MessageArea::CONSOLE_MSGTYPE_SCRIPT: txt << "Script";  break;
            case MessageArea::CONSOLE_MSGTYPE_ACTOR:  txt << "Actor";   break;
            case MessageArea::CONSOLE_MSGTYPE_TERRN:  txt << "Terrn";   break;
            default:;
        }
        txt << "|";
        switch (type)
        {
            case MessageType::CONSOLE_SYSTEM_NOTICE:  txt << "Notice";  break;
            case MessageType::CONSOLE_SYSTEM_ERROR:   txt << "Error";   break;
            case MessageType::CONSOLE_SYSTEM_WARNING: txt << "Warning"; break;
            case MessageType::CONSOLE_SYSTEM_REPLY:   txt << "Success"; break;
            default:;
        }
        txt << "] " << msg;
        Log(txt.ToCStr());
    }

    // Lock and update message list
    std::lock_guard<std::mutex> lock(m_messages_mutex); // Scoped lock
    m_messages.emplace_back(area, type, msg, this->queryMessageTimer(), net_userid, icon);
}

void Console::putMessage(MessageArea area, MessageType type, std::string const& msg, std::string icon)
{
    this->handleMessage(area, type, msg, 0, icon);
}

void Console::putNetMessage(int user_id, MessageType type, const char* text)
{
    this->handleMessage(CONSOLE_MSGTYPE_INFO, type, text, user_id);
}
