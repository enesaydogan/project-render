// *****************************************************************************
// Description:		Header file for the Notification Manager Add-On
// *****************************************************************************

#if !defined _NOTIFICATION_MANAGER_H
#define _NOTIFICATION_MANAGER_H

#define ELEMSTR_LEN				256

bool	GetElementTypeString (const API_ElemType& type, char *elemStr);

void	Do_AttributeMonitor (bool switchOn);
void	Do_CatchProjectEvent (bool switchOn);
void	Do_DefaultMonitor (bool switchOn);
void	Do_ClickedElementMonitor (bool switchOn);
void	Do_ListMonitoredElements ();
void	Do_ElementMonitor (bool switchOn);
void	Do_ToolMonitor (bool switchOn);
void	Do_SelectionMonitor (bool switchOn);
void	Do_ReservationMonitor (bool switchOn);
void	Do_MarkSelElems (void);
void	Do_ListOwnedElements (void);
void	Do_AttachElementURLRef (void);
void	Do_ListElementURLRef (void);
void	Do_LinkElements (void);
void	Do_ClassificationMonitor (bool switchOn);
void	Do_PropertyMonitor (bool switchOn);
void	Do_GraphisoftIDMonitor (bool switchOn);
void	Do_MarqueeIDMonitor (bool switchOn);
void	Do_WindowMonitor (bool switchOn);
void	Do_KeynoteMonitor (bool switchOn);

GSErrCode ElementEventHandlerProc (const API_NotifyElementType *elemType);

#endif	// _NOTIFICATION_MANAGER_H
