/**
 * @file
 * Message Window
 *
 * @authors
 * Copyright (C) 2021 Richard Russon <rich@flatcap.org>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page gui_msgwin Message Window
 *
 * The Message Window is a one-line interactive window at the bottom of the
 * screen.  See @ref gui_mw
 *
 * @defgroup gui_mw GUI: Message Windows
 *
 * The Message Window is a one-line interactive window at the bottom of the
 * screen.  It's used for asking the user questions, displaying messages and
 * for a progress bar.
 *
 * ## Behaviour
 *
 * The Message Window has two modes of behaviour: passive, active.
 *
 * ### Passive
 *
 * Most of the time, the Message Window will be passively displaying messages
 * to the user (or empty).  This is characterised by the Window focus being
 * somewhere else.  In this mode, the Message Window is responsible for drawing
 * itself.
 *
 * @sa mutt_message(), mutt_error()
 *
 * ### Active
 *
 * The Message Window can be hijacked by other code to be used for user
 * interaction, commonly for simple questions, "Are you sure? [Y/n]".
 * In this active state the Window will have focus and it's the responsibility
 * of the hijacker to perform the drawing.
 *
 * @sa query_yesorno(), @ref progress_progress
 *
 * ## Windows
 *
 * | Name           | Type        | Constructor  |
 * | :------------- | :---------- | :----------- |
 * | Message Window | #WT_MESSAGE | msgwin_new() |
 *
 * **Parent**
 * - @ref gui_msgcont
 *
 * **Children**
 * - None
 *
 * ## Data
 * - #MsgWinWindowData
 *
 * The Message Window caches the formatted string.
 *
 * ## Events
 *
 * Once constructed, it is controlled by the following events:
 *
 * | Event Type            | Handler                   |
 * | :-------------------- | :------------------------ |
 * | #NT_WINDOW            | msgwin_window_observer()  |
 * | MuttWindow::recalc()  | msgwin_recalc()           |
 * | MuttWindow::repaint() | msgwin_repaint()          |
 */

#include "config.h"
#include "mutt/lib.h"
#include "msgwin.h"
#include "color/lib.h"
#include "msgcont.h"
#include "msgwin_wdata.h"
#include "mutt_curses.h"
#include "mutt_window.h"

/**
 * msgwin_recalc - Recalculate the display of the Message Window - Implements MuttWindow::recalc() - @ingroup window_recalc
 */
static int msgwin_recalc(struct MuttWindow *win)
{
  if (window_is_focused(win)) // Someone else is using it
    return 0;

  win->actions |= WA_REPAINT;
  mutt_debug(LL_DEBUG5, "recalc done, request WA_REPAINT\n");
  return 0;
}

/**
 * msgwin_repaint - Redraw the Message Window - Implements MuttWindow::repaint() - @ingroup window_repaint
 */
static int msgwin_repaint(struct MuttWindow *win)
{
  if (window_is_focused(win)) // someone else is using it
    return 0;

  struct MsgWinWindowData *wdata = win->wdata;

  mutt_window_move(win, 0, 0);

  mutt_curses_set_normal_backed_color_by_id(wdata->cid);
  mutt_window_move(win, 0, 0);
  mutt_window_addstr(win, wdata->text);
  mutt_curses_set_color_by_id(MT_COLOR_NORMAL);
  mutt_window_clrtoeol(win);

  mutt_debug(LL_DEBUG5, "repaint done\n");
  return 0;
}

/**
 * msgwin_window_observer - Notification that a Window has changed - Implements ::observer_t - @ingroup observer_api
 *
 * This function is triggered by changes to the windows.
 *
 * - State (this window): refresh the window
 * - Delete (this window): clean up the resources held by the Message Window
 */
static int msgwin_window_observer(struct NotifyCallback *nc)
{
  if (nc->event_type != NT_WINDOW)
    return 0;
  if (!nc->global_data || !nc->event_data)
    return -1;

  struct MuttWindow *win = nc->global_data;
  struct EventWindow *ev_w = nc->event_data;
  if (ev_w->win != win)
    return 0;

  if (nc->event_subtype == NT_WINDOW_STATE)
  {
    win->actions |= WA_RECALC;
    mutt_debug(LL_NOTIFY, "window state done, request WA_RECALC\n");
  }
  else if (nc->event_subtype == NT_WINDOW_DELETE)
  {
    notify_observer_remove(win->notify, msgwin_window_observer, win);
    mutt_debug(LL_DEBUG5, "window delete done\n");
  }
  return 0;
}

/**
 * msgwin_new - Create the Message Window
 * @retval ptr New Window
 */
struct MuttWindow *msgwin_new(void)
{
  struct MuttWindow *win = mutt_window_new(WT_MESSAGE, MUTT_WIN_ORIENT_VERTICAL, MUTT_WIN_SIZE_FIXED,
                                           MUTT_WIN_SIZE_UNLIMITED, 1);
  win->wdata = msgwin_wdata_new();
  win->wdata_free = msgwin_wdata_free;
  win->recalc = msgwin_recalc;
  win->repaint = msgwin_repaint;

  notify_observer_add(win->notify, NT_WINDOW, msgwin_window_observer, win);

  return win;
}

/**
 * msgwin_get_text - Get the text from the Message Window
 * @param win Message Window
 * @retval ptr Window text
 *
 * @note Do not free the returned string
 */
const char *msgwin_get_text(struct MuttWindow *win)
{
  if (!win)
    win = msgcont_get_msgwin();
  if (!win)
    return NULL;

  struct MsgWinWindowData *wdata = win->wdata;

  return wdata->text;
}

/**
 * msgwin_set_text - Set the text for the Message Window
 * @param win Message Window
 * @param text Text to set
 * @param cid  Colour Id, e.g. #MT_COLOR_MESSAGE
 *
 * @note The text string will be copied
 */
void msgwin_set_text(struct MuttWindow *win, const char *text, enum ColorId cid)
{
  if (!win)
    win = msgcont_get_msgwin();
  if (!win)
    return;

  struct MsgWinWindowData *wdata = win->wdata;

  wdata->cid = cid;
  mutt_str_replace(&wdata->text, text);

  win->actions |= WA_RECALC;
}

/**
 * msgwin_clear_text - Clear the text in the Message Window
 * @param win Message Window
 */
void msgwin_clear_text(struct MuttWindow *win)
{
  msgwin_set_text(win, NULL, MT_COLOR_NORMAL);
}

/**
 * msgwin_get_window - Get the Message Window pointer
 * @retval ptr Message Window
 *
 * Allow some users direct access to the Message Window.
 */
struct MuttWindow *msgwin_get_window(void)
{
  return msgcont_get_msgwin();
}

/**
 * msgwin_get_width - Get the width of the Message Window
 * @retval num Width of Message Window
 */
size_t msgwin_get_width(void)
{
  struct MuttWindow *win = msgcont_get_msgwin();
  if (!win)
    return 0;

  return win->state.cols;
}

/**
 * msgwin_set_height - Resize the Message Window
 * @param height Number of rows required
 *
 * Resize the other Windows to allow a multi-line message to be displayed.
 */
void msgwin_set_height(short height)
{
  struct MuttWindow *win = msgcont_get_msgwin();
  if (!win)
    return;

  if (height < 1)
    height = 1;
  else if (height > 3)
    height = 3;

  struct MuttWindow *win_cont = win->parent;

  win_cont->req_rows = height;
  mutt_window_reflow(win_cont->parent);
}
