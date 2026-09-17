/*! \file IspMaster.h \brief xxx */
/***************************************************************************
 *            IspMaster.h
 *
 ****************************************************************************/
///	\ingroup software
///	\defgroup 
///	\code #include "IspMaster.h" \endcode
//****************************************************************************/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

//@{
#ifndef _ISPMASTER_H_

#define _ISPMASTER_H_
	
#ifdef ISP_MASTER
// only if acivated


// essentielle Includes
// ================================================================

#include "defports.h"
 
 
// Port-Definitionen
// ================================================================
#ifdef iTelex 
DEFPORTTRI(IspResetOut, F, 5)
#endif //def iTelex

#ifdef iTelex_Light
DEFPORTTRI(IspResetOut, D, 3)
#endif //def iTelex_Light


extern void InitIspMaster();

#endif //def ISP_MASTER

	
#endif /* _ISPMASTER_H_ */
//@}

