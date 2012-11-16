﻿/*
  Q Light Controller
  hpmprivate-win32.cpp

  Copyright (c) Heikki Junnila

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  Version 2 as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details. The license is
  in the file "COPYING".

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// Let's assume we have at least W2K (http://msdn.microsoft.com/en-us/library/Aa383745)
#define _WIN32_WINNT 0x05000000
#define _WIN32_WINDOWS 0x05000000
#define WINVER 0x05000000
#include <Windows.h>
#include <Dbt.h>

#include <QWidget>
#include <QDebug>

#include "hotplugmonitor.h"
#include "hpmprivate-win32.h"

#define DBCC_NAME_RAWDEVICE_USB_GUID "a5dcbf10-6530-11d2-901f-00c04fb951ed"
#define DBCC_NAME_SEPARATOR "#"
#define DBCC_NAME_VID "VID_"
#define DBCC_NAME_PID "PID_"
#define DBCC_NAME_VIDPID_SEPARATOR "&"

static const GUID USBClassGUID =
    { 0x25dbce51, 0x6c8f, 0x4a72, { 0x8a, 0x6d, 0xb5, 0x4c, 0x2b, 0x4f, 0xc8, 0x35 } };

/****************************************************************************
 * HPMPrivate implementation
 ****************************************************************************/

HPMPrivate::HPMPrivate(HotPlugMonitor* parent)
    : QWidget(0) // This class has to be a widget to receive winEvent() events
    , m_hpm(parent)
    , m_hDeviceNotify(NULL)
{
    Q_ASSERT(parent != NULL);
}

HPMPrivate::~HPMPrivate()
{
}

void HPMPrivate::start()
{
    DEV_BROADCAST_DEVICEINTERFACE notificationFilter;

    ZeroMemory(&notificationFilter, sizeof(notificationFilter));
    notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = USBClassGUID;

    m_hDeviceNotify = RegisterDeviceNotification(winId(),
                                                 &notificationFilter,
                                                 DEVICE_NOTIFY_WINDOW_HANDLE |
                                                 DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);

    if (m_hDeviceNotify == NULL)
        qWarning() << Q_FUNC_INFO << "Unable to register device notification.";
}

void HPMPrivate::stop()
{
    if (m_hDeviceNotify == NULL)
        return;
    else if (UnregisterDeviceNotification(m_hDeviceNotify) == FALSE)
        qWarning() << Q_FUNC_INFO << "Unable to unregister device notification.";
    m_hDeviceNotify = NULL;
}

bool HPMPrivate::extractVidPid(const QString& dbccName, uint* vid, uint* pid)
{
    Q_ASSERT(vid != NULL);
    Q_ASSERT(pid != NULL);

    // This function assumes $dbccName contains something like:
    // "\\?\USB#Vid_xxxx&Pid_yyyy#zzzzzzzzz#{sssssssssssssssssss}"
    // The string is first split into 3 parts at the hash marks (#),
    // and the part starting with "Vid_" is taken into further inspection.
    // That substring is again parsed into two parts at ampersand mark (&).
    // Of those two strings, the leading "vid_" and "pid_" is removed, leaving
    // only the hexadecimal values for VID and PID which are converted to uints.
    QStringList parts = dbccName.toUpper().split(DBCC_NAME_SEPARATOR);
    for (int i = 0; i < parts.size(); i++)
    {
        if (parts[i].startsWith(DBCC_NAME_VID) == true)
        {
            QStringList vidpid = parts[i].split(DBCC_NAME_VIDPID_SEPARATOR);
            if (vidpid.size() != 2)
                return false;

            QString v = vidpid[0].remove(DBCC_NAME_VID);
            QString p = vidpid[1].remove(DBCC_NAME_PID);
            *vid = v.toUInt(0, 16);
            *pid = p.toUInt(0, 16);

            return true;
        }
    }

    return false;
}

bool HPMPrivate::winEvent(MSG* message, long* RESULT)
{
    Q_UNUSED(RESULT)
    Q_ASSERT(message != NULL);

    UINT msg = message->message;
    WPARAM wParam = message->wParam;
    LPARAM lParam = message->lParam;

    // We're only interested in device change events
    if (msg != WM_DEVICECHANGE)
        return false;

    PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR) lParam;
    if (wParam == DBT_DEVICEARRIVAL)
    {
        // A new device has been added to the system
        Q_ASSERT(hdr != NULL);
        if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
        {
            PDEV_BROADCAST_DEVICEINTERFACE dev = (PDEV_BROADCAST_DEVICEINTERFACE) hdr;
            QString dbcc_name(QString::fromWCharArray(dev->dbcc_name));
            if (dbcc_name.contains(DBCC_NAME_RAWDEVICE_USB_GUID) == true)
            {
                // Emit only raw USB devices
                uint vid = 0, pid = 0;
                if (extractVidPid(dbcc_name, &vid, &pid) == true)
                    m_hpm->emitDeviceAdded(vid, pid);
            }
        }
    }
    else if (wParam == DBT_DEVICEREMOVECOMPLETE)
    {
        // An existing device has been removed from the system
        Q_ASSERT(hdr != NULL);
        if (hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
        {
            PDEV_BROADCAST_DEVICEINTERFACE dev = (PDEV_BROADCAST_DEVICEINTERFACE) hdr;
            QString dbccName(QString::fromWCharArray(dev->dbcc_name));
            if (dbccName.contains(DBCC_NAME_RAWDEVICE_USB_GUID) == true)
            {
                // Emit only raw USB devices
                uint vid = 0, pid = 0;
                if (extractVidPid(dbccName, &vid, &pid) == true)
                    m_hpm->emitDeviceRemoved(vid, pid);
            }
        }
    }

    // Let Qt act on all events regardless of whether we recognize them or not
    return false;
}
