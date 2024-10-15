/* $Id: vcsrevisions.js 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Common JavaScript functions
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/**
 * @internal.
 */
function vcsRevisionFormatDate(tsDate)
{
    /*return tsDate.toLocaleDateString();*/
    return tsDate.toISOString().split('T')[0];
}

/**
 * @internal.
 */
function vcsRevisionFormatTime(tsDate)
{
    return formatTimeHHMM(tsDate, true /*fNbsp*/);
}

/**
 * Called 'onclick' for the link/button used to show the detailed VCS
 * revisions.
 * @internal.
 */
function vcsRevisionShowDetails(oElmSource)
{
    document.getElementById('vcsrevisions-detailed').style.display = 'block';
    document.getElementById('vcsrevisions-brief').style.display = 'none';
    oElmSource.style.display = 'none';
    return false;
}

/**
 * Called when we've got the revision data.
 * @internal
 */
function vcsRevisionsRender(sTestMgr, oElmDst, sBugTracker, oRestReq, sUrl)
{
    console.log('vcsRevisionsRender: status=' + oRestReq.status + ' readyState=' + oRestReq.readyState + ' url=' + sUrl);
    if (oRestReq.readyState != oRestReq.DONE)
    {
        oElmDst.innerHTML = '<p>' + oRestReq.readyState + '</p>';
        return true;
    }


    /*
     * Check the result and translate it to a javascript object (oResp).
     */
    var oResp = null;
    var sHtml;
    if (oRestReq.status != 200)
    {
        /** @todo figure why this doesn't work (sPath to something random). */
        var sMsg = oRestReq.getResponseHeader('tm-error-message');
        console.log('vcsRevisionsRender: status=' + oRestReq.status + ' readyState=' + oRestReq.readyState + ' url=' + sUrl + ' msg=' + sMsg);
        sHtml  = '<p>error: status=' + oRestReq.status + 'readyState=' + oRestReq.readyState + ' url=' + sUrl;
        if (sMsg)
            sHtml += ' msg=' + sMsg;
        sHtml += '</p>';
    }
    else
    {
        try
        {
            oResp = JSON.parse(oRestReq.responseText);
        }
        catch (oEx)
        {
            console.log('JSON.parse threw: ' + oEx.toString());
            console.log(oRestReq.responseText);
            sHtml = '<p>error: JSON.parse threw: ' + oEx.toString() + '</p>';
        }
    }

    /*
     * Do the rendering.
     */
    if (oResp)
    {
        if (oResp.cCommits == 0)
        {
            sHtml = '<p>None.</p>';
        }
        else
        {
            var aoCommits = oResp.aoCommits;
            var cCommits  = oResp.aoCommits.length;
            var i;

            sHtml = '';
            /*sHtml = '<a href="#" onclick="return vcsRevisionShowDetails(this);" class="vcsrevisions-show-details">Show full VCS details...</a>\n';*/
            /*sHtml = '<button onclick="vcsRevisionShowDetails(this);" class="vcsrevisions-show-details">Show full VCS details...</button>\n';*/

            /* Brief view (the default): */
            sHtml += '<p id="vcsrevisions-brief">';
            for (i = 0; i < cCommits; i++)
            {
                var oCommit = aoCommits[i];
                var sUrl    = oResp.sTracChangesetUrlFmt.replace('%(sRepository)s', oCommit.sRepository).replace('%(iRevision)s', oCommit.iRevision.toString());
                var sTitle  = oCommit.sAuthor + ': ' + oCommit.sMessage;
                sHtml += ' <a href="' + escapeElem(sUrl) + '" title="' + escapeElem(sTitle) + '">r' + oCommit.iRevision + '</a> \n';
            }
            sHtml += '</p>';
            sHtml += '<a href="#" onclick="return vcsRevisionShowDetails(this);" class="vcsrevisions-show-details-bottom">Show full VCS details...</a>\n';

            /* Details view: */
            sHtml += '<div id="vcsrevisions-detailed" style="display:none;">\n';
            var iCurDay = null;
            if (0)
            {
                /* Changelog variant: */
                for (i = 0; i < cCommits; i++)
                {
                    var oCommit    = aoCommits[i];
                    var tsCreated  = parseIsoTimestamp(oCommit.tsCreated);
                    var sUrl       = oResp.sTracChangesetUrlFmt.replace('%(sRepository)s', oCommit.sRepository).replace('%(iRevision)s', oCommit.iRevision.toString());
                    var iCommitDay = Math.floor((tsCreated.getTime() + tsCreated.getTimezoneOffset()) / (24 * 60 * 60 * 1000));
                    if (iCurDay === null || iCurDay != iCommitDay)
                    {
                        if (iCurDay !== null)
                            sHtml += ' </dl>\n';
                        iCurDay = iCommitDay;
                        sHtml += ' <h3>' + vcsRevisionFormatDate(tsCreated) + ' ' + g_kasDaysOfTheWeek[tsCreated.getDay()] + '</h3>\n';
                        sHtml += ' <dl>\n';
                    }

                    sHtml += '  <dt id="r' + oCommit.iRevision + '">';
                    sHtml += '<a href="' + oResp.sTracChangesetUrlFmt.replace('%(iRevision)s', oCommit.iRevision.toString()) + '">';
                    /*sHtml += '<span class="vcsrevisions-time">' + escapeElem(vcsRevisionFormatTime(tsCreated)) + '</span>'
                    sHtml += ' Changeset <span class="vcsrevisions-rev">r' + oCommit.iRevision + '</span>';
                    sHtml += ' by <span class="vcsrevisions-author">' + escapeElem(oCommit.sAuthor) + '</span>'; */
                    sHtml += '<span class="vcsrevisions-time">' + escapeElem(vcsRevisionFormatTime(tsCreated)) + '</span>';
                    sHtml += ' - <span class="vcsrevisions-rev">r' + oCommit.iRevision + '</span>';
                    sHtml += ' - <span class="vcsrevisions-author">' + escapeElem(oCommit.sAuthor) + '</span>';
                    sHtml += '</a></dt>\n';
                    sHtml += '  <dd>' + escapeElem(oCommit.sMessage) + '</dd>\n';
                }

                if (iCurDay !== null)
                    sHtml += ' </dl>\n';
            }
            else
            {   /* TABLE variant: */
                sHtml += '<table class="vcsrevisions-table">';
                var iAlt = 0;
                for (i = 0; i < cCommits; i++)
                {
                    var oCommit    = aoCommits[i];
                    var tsCreated  = parseIsoTimestamp(oCommit.tsCreated);
                    var sUrl       = oResp.sTracChangesetUrlFmt.replace('%(sRepository)s', oCommit.sRepository).replace('%(iRevision)s', oCommit.iRevision.toString());
                    var iCommitDay = Math.floor((tsCreated.getTime() + tsCreated.getTimezoneOffset()) / (24 * 60 * 60 * 1000));
                    if (iCurDay === null || iCurDay != iCommitDay)
                    {
                        iCurDay = iCommitDay;
                        sHtml += '<tr id="r' + oCommit.iRevision + '"><td colspan="4" class="vcsrevisions-tab-date">';
                        sHtml += vcsRevisionFormatDate(tsCreated) + ' ' + g_kasDaysOfTheWeek[tsCreated.getDay()];
                        sHtml += '</td></tr>\n';
                        sHtml += '<tr>';
                        iAlt = 0;
                    }
                    else
                        sHtml += '<tr id="r' + oCommit.iRevision + '">';
                    var sAltCls = '';
                    var sAltClsStmt = '';
                    iAlt += 1;
                    if (iAlt & 1)
                    {
                        sAltCls     = ' alt';
                        sAltClsStmt = ' class="alt"';
                    }
                    sHtml += '<td class="vcsrevisions-tab-time'+sAltCls+'"><a href="' + sUrl + '">'
                           + escapeElem(vcsRevisionFormatTime(tsCreated)) + '</a></td>';
                    sHtml += '<td'+sAltClsStmt+'><a href="' + sUrl + '" class="vcsrevisions-rev' + sAltCls + '">r'
                           + oCommit.iRevision + '</a></td>';
                    sHtml += '<td'+sAltClsStmt+'><a href="' + sUrl + '" class="vcsrevisions-author' + sAltCls + '">'
                           + escapeElem(oCommit.sAuthor) + '<a></td>';
                    sHtml += '<td'+sAltClsStmt+'>' + escapeElem(oCommit.sMessage) + '</td></tr>\n';
                }
                sHtml += '</table>\n';
            }
            sHtml += '</div>\n';
        }
    }

    oElmDst.innerHTML = sHtml;
}

/** Called by the xtracker bugdetails page. */
function VcsRevisionsLoad(sTestMgr, oElmDst, sBugTracker, lBugNo)
{
    oElmDst.innerHTML = '<p>Loading VCS revisions...</p>';

    var sUrl = sTestMgr + 'rest.py?sPath=vcs/bugreferences/' + sBugTracker + '/' + lBugNo;
    var oRestReq = new XMLHttpRequest();
    oRestReq.onreadystatechange = function() { vcsRevisionsRender(sTestMgr, oElmDst, sBugTracker, this, sUrl); }
    oRestReq.open('GET', sUrl);
    oRestReq.withCredentials = true;
    /*oRestReq.setRequestHeader('Content-type', 'application/json'); - Causes CORS trouble. */
    oRestReq.send();
}

