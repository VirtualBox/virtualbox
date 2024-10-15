/* $Id: common.js 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Same as WuiDispatcherBase.ksParamRedirectTo. */
var g_ksParamRedirectTo = 'RedirectTo';

/** Days of the week in Date() style with Sunday first. */
var g_kasDaysOfTheWeek = [ 'Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday' ];


/**
 * Detects the firefox browser.
 */
function isBrowserFirefox()
{
    return typeof InstallTrigger !== 'undefined';
}

/**
 * Detects the google chrome browser.
 * @note Might be confused with edge chromium
 */
function isBrowserChrome()
{
    var oChrome = window.chrome;
    if (!oChrome)
        return false;
    return !!oChrome.runtime || !oChrome.webstore;
}

/**
 * Detects the chromium-based edge browser.
 */
function isBrowserEdgeChromium()
{
    if (!isBrowserChrome())
        return false;
    return navigation.userAgent.indexOf('Edg') >= 0
}

/**
 * Detects the chromium-based edge browser.
 */
function isBrowserInternetExplorer()
{
    /* documentMode is an IE only property. Values are 5,7,8,9,10 or 11
       according to google results. */
    if (typeof document.documentMode !== 'undefined')
    {
        if (document.documentMode)
            return true;
    }
    /* IE only conditional compiling feature.  Here, the 'true || ' part
       will be included in the if when executing in IE: */
    if (/*@cc_on true || @*/false)
        return true;
    return false;
}

/**
 * Detects the safari browser (v3+).
 */
function isBrowserSafari()
{
    /* Check if window.HTMLElement is a function named 'HTMLElementConstructor()'?
       Should work for older safari versions. */
    var sStr = window.HTMLElement.toString();
    if (/constructor/i.test(sStr))
        return true;

    /* Check the class name of window.safari.pushNotification.  This works for current. */
    var oSafari = window['safari'];
    if (oSafari)
    {
        if (typeof oSafari !== 'undefined')
        {
            var oPushNotify = oSafari.pushNotification;
            if (oPushNotify)
            {
                sStr = oPushNotify.toString();
                if (/\[object Safari.*Notification\]/.test(sStr))
                    return true;
            }
        }
    }
    return false;
}

/**
 * Checks if the given value is a decimal integer value.
 *
 * @returns true if it is, false if it's isn't.
 * @param   sValue              The value to inspect.
 */
function isInteger(sValue)
{
    if (typeof sValue != 'undefined')
    {
        var intRegex = /^\d+$/;
        if (intRegex.test(sValue))
        {
            return true;
        }
    }
    return false;
}

/**
 * Checks if @a oMemmber is present in aoArray.
 *
 * @returns true/false.
 * @param   aoArray             The array to check.
 * @param   oMember             The member to check for.
 */
function isMemberOfArray(aoArray, oMember)
{
    var i;
    for (i = 0; i < aoArray.length; i++)
        if (aoArray[i] == oMember)
            return true;
    return false;
}

/**
 * Parses a typical ISO timestamp, returing a Date object, reasonably
 * forgiving, but will throw weird indexing/conversion errors if the input
 * is malformed.
 *
 * @returns Date object.
 * @param   sTs             The timestamp to parse.
 * @sa      parseIsoTimestamp() in utils.py.
 */
function parseIsoTimestamp(sTs)
{
    /* YYYY-MM-DD */
    var iYear  = parseInt(sTs.substring(0, 4), 10);
    console.assert(sTs.charAt(4) == '-');
    var iMonth = parseInt(sTs.substring(5, 7), 10);
    console.assert(sTs.charAt(7) == '-');
    var iDay   = parseInt(sTs.substring(8, 10), 10);

    /* Skip separator */
    var sTime = sTs.substring(10);
    while ('Tt \t\n\r'.includes(sTime.charAt(0))) {
        sTime = sTime.substring(1);
    }

    /* HH:MM[:SS[.fraction] */
    var iHour = parseInt(sTime.substring(0, 2), 10);
    console.assert(sTime.charAt(2) == ':');
    var iMin  = parseInt(sTime.substring(3, 5), 10);
    var iSec          = 0;
    var iMicroseconds = 0;
    var offTime       = 5;
    if (sTime.charAt(5) == ':')
    {
        iSec  = parseInt(sTime.substring(6, 8), 10);

        /* Fraction? */
        offTime = 8;
        if (offTime < sTime.length && '.,'.includes(sTime.charAt(offTime)))
        {
            offTime += 1;
            var cchFraction = 0;
            while (offTime + cchFraction < sTime.length && '0123456789'.includes(sTime.charAt(offTime + cchFraction)))
                cchFraction += 1;
            if (cchFraction > 0)
            {
                iMicroseconds = parseInt(sTime.substring(offTime, offTime + cchFraction), 10);
                offTime += cchFraction;
                while (cchFraction < 6)
                {
                    iMicroseconds *= 10;
                    cchFraction += 1;
                }
                while (cchFraction > 6)
                {
                    iMicroseconds = iMicroseconds / 10;
                    cchFraction -= 1;
                }
            }
        }
    }
    var iMilliseconds = (iMicroseconds + 499) / 1000;

    /* Naive? */
    var oDate = new Date(Date.UTC(iYear, iMonth - 1, iDay, iHour, iMin, iSec, iMilliseconds));
    if (offTime >= sTime.length)
        return oDate;

    /* Zulu? */
    if (offTime >= sTime.length || 'Zz'.includes(sTime.charAt(offTime)))
        return oDate;

    /* Some kind of offset afterwards. */
    var chSign = sTime.charAt(offTime);
    if ('+-'.includes(chSign))
    {
        offTime += 1;
        var cMinTz = parseInt(sTime.substring(offTime, offTime + 2), 10) * 60;
        offTime += 2;
        if (offTime  < sTime.length && sTime.charAt(offTime) == ':')
            offTime += 1;
        if (offTime + 2 <= sTime.length)
        {
            cMinTz += parseInt(sTime.substring(offTime, offTime + 2), 10);
            offTime += 2;
        }
        console.assert(offTime == sTime.length);
        if (chSign == '-')
            cMinTz = -cMinTz;

        return new Date(oDate.getTime() - cMinTz * 60000);
    }
    console.assert(false);
    return oDate;
}

/**
 * @param   oDate   Date object.
 */
function formatTimeHHMM(oDate, fNbsp)
{
    var sTime = oDate.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit'} );
    if (fNbsp === true)
        sTime = sTime.replace(' ', '\u00a0');

    /* Workaround for single digit hours in firefox with en_US (minutes works fine): */
    var iHours = oDate.getHours();
    if ((iHours % 12) < 10)
    {
        var ch1 = sTime.substr(0, 1);
        var ch2 = sTime.substr(1, 1);
        if (  ch1 == (iHours % 12).toString()
            && !(ch2 >= '0' && ch2 <= '9'))
            sTime = '0' + sTime;
    }
    return sTime;
}

/**
 * Escapes special characters to HTML-safe sequences, for element use.
 *
 * @returns Escaped string suitable for HTML.
 * @param   sText               Plain text to escape.
 */
function escapeElem(sText)
{
    sText = sText.replace(/&/g, '&amp;');
    sText = sText.replace(/>/g, '&lt;');
    return  sText.replace(/</g, '&gt;');
}

/**
 * Escapes special characters to HTML-safe sequences, for double quoted
 * attribute use.
 *
 * @returns Escaped string suitable for HTML.
 * @param   sText               Plain text to escape.
 */
function escapeAttr(sText)
{
    sText = sText.replace(/&/g, '&amp;');
    sText = sText.replace(/</g, '&lt;');
    sText = sText.replace(/>/g, '&gt;');
    return  sText.replace(/"/g, '&quot;');
}

/**
 * Removes the element with the specified ID.
 */
function removeHtmlNode(sContainerId)
{
    var oElement = document.getElementById(sContainerId);
    if (oElement)
    {
        oElement.parentNode.removeChild(oElement);
    }
}

/**
 * Sets the value of the element with id @a sInputId to the keys of aoItems
 * (comma separated).
 */
function setElementValueToKeyList(sInputId, aoItems)
{
    var sKey;
    var oElement = document.getElementById(sInputId);
    oElement.value = '';

    for (sKey in aoItems)
    {
        if (oElement.value.length > 0)
        {
            oElement.value += ',';
        }

        oElement.value += sKey;
    }
}

/**
 * Get the Window.devicePixelRatio in a safe way.
 *
 * @returns Floating point ratio. 1.0 means it's a 1:1 ratio.
 */
function getDevicePixelRatio()
{
    var fpRatio = 1.0;
    if (window.devicePixelRatio)
    {
        fpRatio = window.devicePixelRatio;
        if (fpRatio < 0.5 || fpRatio > 10.0)
            fpRatio = 1.0;
    }
    return fpRatio;
}

/**
 * Tries to figure out the DPI of the device in the X direction.
 *
 * @returns DPI on success, null on failure.
 */
function getDeviceXDotsPerInch()
{
    if (window.deviceXDPI && window.deviceXDPI > 48 && window.deviceXDPI < 2048)
    {
        return window.deviceXDPI;
    }
    else if (window.devicePixelRatio && window.devicePixelRatio >= 0.5 && window.devicePixelRatio <= 10.0)
    {
        cDotsPerInch = Math.round(96 * window.devicePixelRatio);
    }
    else
    {
        cDotsPerInch = null;
    }
    return cDotsPerInch;
}

/**
 * Gets the width of the given element (downscaled).
 *
 * Useful when using the element to figure the size of a image
 * or similar.
 *
 * @returns Number of pixels.  null if oElement is bad.
 * @param   oElement        The element (not ID).
 */
function getElementWidth(oElement)
{
    if (oElement && oElement.offsetWidth)
        return oElement.offsetWidth;
    return null;
}

/** By element ID version of getElementWidth. */
function getElementWidthById(sElementId)
{
    return getElementWidth(document.getElementById(sElementId));
}

/**
 * Gets the real unscaled width of the given element.
 *
 * Useful when using the element to figure the size of a image
 * or similar.
 *
 * @returns Number of screen pixels.  null if oElement is bad.
 * @param   oElement        The element (not ID).
 */
function getUnscaledElementWidth(oElement)
{
    if (oElement && oElement.offsetWidth)
        return Math.round(oElement.offsetWidth * getDevicePixelRatio());
    return null;
}

/** By element ID version of getUnscaledElementWidth. */
function getUnscaledElementWidthById(sElementId)
{
    return getUnscaledElementWidth(document.getElementById(sElementId));
}

/**
 * Gets the part of the URL needed for a RedirectTo parameter.
 *
 * @returns URL string.
 */
function getCurrentBrowerUrlPartForRedirectTo()
{
    var sWhere = window.location.href;
    var offTmp;
    var offPathKeep;

    /* Find the end of that URL 'path' component. */
    var offPathEnd = sWhere.indexOf('?');
    if (offPathEnd < 0)
        offPathEnd = sWhere.indexOf('#');
    if (offPathEnd < 0)
        offPathEnd = sWhere.length;

    /* Go backwards from the end of the and find the start of the last component. */
    offPathKeep = sWhere.lastIndexOf("/", offPathEnd);
    offTmp = sWhere.lastIndexOf(":", offPathEnd);
    if (offPathKeep < offTmp)
        offPathKeep = offTmp;
    offTmp = sWhere.lastIndexOf("\\", offPathEnd);
    if (offPathKeep < offTmp)
        offPathKeep = offTmp;

    return sWhere.substring(offPathKeep + 1);
}

/**
 * Adds the given sorting options to the URL and reloads.
 *
 * This will preserve previous sorting columns except for those
 * given in @a aiColumns.
 *
 * @param   sParam              Sorting parameter.
 * @param   aiColumns           Array of sorting columns.
 */
function ahrefActionSortByColumns(sParam, aiColumns)
{
    var sWhere = window.location.href;

    var offHash = sWhere.indexOf('#');
    if (offHash < 0)
        offHash = sWhere.length;

    var offQm = sWhere.indexOf('?');
    if (offQm > offHash)
        offQm = -1;

    var sNew = '';
    if (offQm > 0)
        sNew = sWhere.substring(0, offQm);

    sNew += '?' + sParam + '=' + aiColumns[0];
    var i;
    for (i = 1; i < aiColumns.length; i++)
        sNew += '&' + sParam + '=' + aiColumns[i];

    if (offQm >= 0 && offQm + 1 < offHash)
    {
        var sArgs = '&' + sWhere.substring(offQm + 1, offHash);
        var off   = 0;
        while (off < sArgs.length)
        {
            var offMatch = sArgs.indexOf('&' + sParam + '=', off);
            if (offMatch >= 0)
            {
                if (off < offMatch)
                    sNew += sArgs.substring(off, offMatch);

                var offValue = offMatch + 1 + sParam.length + 1;
                offEnd = sArgs.indexOf('&', offValue);
                if (offEnd < offValue)
                    offEnd = sArgs.length;

                var iColumn = parseInt(sArgs.substring(offValue, offEnd));
                if (!isMemberOfArray(aiColumns, iColumn) && !isMemberOfArray(aiColumns, -iColumn))
                    sNew += sArgs.substring(offMatch, offEnd);

                off = offEnd;
            }
            else
            {
                sNew += sArgs.substring(off);
                break;
            }
        }
    }

    if (offHash < sWhere.length)
        sNew = sWhere.substr(offHash);

    window.location.href = sNew;
}

/**
 * Sets the value of an input field element (give by ID).
 *
 * @returns Returns success indicator (true/false).
 * @param   sFieldId            The field ID (required for updating).
 * @param   sValue              The field value.
 */
function setInputFieldValue(sFieldId, sValue)
{
    var oInputElement = document.getElementById(sFieldId);
    if (oInputElement)
    {
        oInputElement.value = sValue;
        return true;
    }
    return false;
}

/**
 * Adds a hidden input field to a form.
 *
 * @returns The new input field element.
 * @param   oFormElement        The form to append it to.
 * @param   sName               The field name.
 * @param   sValue              The field value.
 * @param   sFieldId            The field ID (optional).
 */
function addHiddenInputFieldToForm(oFormElement, sName, sValue, sFieldId)
{
    var oNew = document.createElement('input');
    oNew.type  = 'hidden';
    oNew.name  = sName;
    oNew.value = sValue;
    if (sFieldId)
        oNew.id = sFieldId;
    oFormElement.appendChild(oNew);
    return oNew;
}

/** By element ID version of addHiddenInputFieldToForm. */
function addHiddenInputFieldToFormById(sFormId, sName, sValue, sFieldId)
{
    return addHiddenInputFieldToForm(document.getElementById(sFormId), sName, sValue, sFieldId);
}

/**
 * Adds or updates a hidden input field to/on a form.
 *
 * @returns The new input field element.
 * @param   sFormId             The ID of the form to amend.
 * @param   sName               The field name.
 * @param   sValue              The field value.
 * @param   sFieldId            The field ID (required for updating).
 */
function addUpdateHiddenInputFieldToFormById(sFormId, sName, sValue, sFieldId)
{
    var oInputElement = null;
    if (sFieldId)
    {
        oInputElement = document.getElementById(sFieldId);
    }
    if (oInputElement)
    {
        oInputElement.name  = sName;
        oInputElement.value = sValue;
    }
    else
    {
        oInputElement = addHiddenInputFieldToFormById(sFormId, sName, sValue, sFieldId);
    }
    return oInputElement;
}

/**
 * Adds a width and a dpi input to the given form element if possible to
 * determine the values.
 *
 * This is normally employed in an onlick hook, but then you must specify IDs or
 * the browser may end up adding it several times.
 *
 * @param   sFormId             The ID of the form to amend.
 * @param   sWidthSrcId         The ID of the element to calculate the width
 *                              value from.
 * @param   sWidthName          The name of the width value.
 * @param   sDpiName            The name of the dpi value.
 */
function addDynamicGraphInputs(sFormId, sWidthSrcId, sWidthName, sDpiName)
{
    var cx            = getUnscaledElementWidthById(sWidthSrcId);
    var cDotsPerInch  = getDeviceXDotsPerInch();

    if (cx)
    {
        addUpdateHiddenInputFieldToFormById(sFormId, sWidthName, cx, sFormId + '-' + sWidthName + '-id');
    }

    if (cDotsPerInch)
    {
        addUpdateHiddenInputFieldToFormById(sFormId, sDpiName, cDotsPerInch, sFormId + '-' + sDpiName + '-id');
    }

}

/**
 * Adds the RedirecTo field with the current URL to the form.
 *
 * This is a 'onsubmit' action.
 *
 * @returns Returns success indicator (true/false).
 * @param   oForm               The form being submitted.
 */
function addRedirectToInputFieldWithCurrentUrl(oForm)
{
    /* Constant used here is duplicated in WuiDispatcherBase.ksParamRedirectTo */
    return addHiddenInputFieldToForm(oForm, 'RedirectTo', getCurrentBrowerUrlPartForRedirectTo(), null);
}

/**
 * Adds the RedirecTo parameter to the href of the given anchor.
 *
 * This is a 'onclick' action.
 *
 * @returns Returns success indicator (true/false).
 * @param   oAnchor         The anchor element being clicked on.
 */
function addRedirectToAnchorHref(oAnchor)
{
    var sRedirectToParam = g_ksParamRedirectTo + '=' + encodeURIComponent(getCurrentBrowerUrlPartForRedirectTo());
    var sHref = oAnchor.href;
    if (sHref.indexOf(sRedirectToParam) < 0)
    {
        var sHash;
        var offHash = sHref.indexOf('#');
        if (offHash >= 0)
            sHash = sHref.substring(offHash);
        else
        {
            sHash   = '';
            offHash = sHref.length;
        }
        sHref = sHref.substring(0, offHash)
        if (sHref.indexOf('?') >= 0)
            sHref += '&';
        else
            sHref += '?';
        sHref += sRedirectToParam;
        sHref += sHash;
        oAnchor.href = sHref;
    }
    return true;
}



/**
 * Clears one input element.
 *
 * @param   oInput      The input to clear.
 */
function resetInput(oInput)
{
    switch (oInput.type)
    {
        case 'checkbox':
        case 'radio':
            oInput.checked = false;
            break;

        case 'text':
            oInput.value = 0;
            break;
    }
}


/**
 * Clears a form.
 *
 * @param   sIdForm     The ID of the form
 */
function clearForm(sIdForm)
{
    var oForm = document.getElementById(sIdForm);
    if (oForm)
    {
        var aoInputs = oForm.getElementsByTagName('INPUT');
        var i;
        for (i = 0; i < aoInputs.length; i++)
            resetInput(aoInputs[i])

        /* HTML5 allows inputs outside <form>, so scan the document. */
        aoInputs = document.getElementsByTagName('INPUT');
        for (i = 0; i < aoInputs.length; i++)
            if (aoInputs.hasOwnProperty("form"))
                if (aoInputs.form == sIdForm)
                    resetInput(aoInputs[i])
    }

    return true;
}


/**
 * Used by the time navigation to update the hidden efficient date field when
 * either of the date or time fields changes.
 *
 * @param oForm     The form.
 */
function timeNavigationUpdateHiddenEffDate(oForm, sIdSuffix)
{
    var sDate = document.getElementById('EffDate' + sIdSuffix).value;
    var sTime = document.getElementById('EffTime' + sIdSuffix).value;

    var oField = document.getElementById('EffDateTime' + sIdSuffix);
    oField.value =  sDate + 'T' + sTime + '.00Z';
}


/** @name Collapsible / Expandable items
 * @{
 */


/**
 * Toggles the collapsible / expandable state of a parent DD and DT uncle.
 *
 * @returns true
 * @param   oAnchor             The anchor object.
 */
function toggleCollapsibleDtDd(oAnchor)
{
    var oParent = oAnchor.parentElement;
    var sClass  = oParent.className;

    /* Find the DD sibling tag */
    var oDdElement = oParent.nextSibling;
    while (oDdElement != null && oDdElement.tagName != 'DD')
        oDdElement = oDdElement.nextSibling;

    /* Determin the new class and arrow char. */
    var sNewClass;
    var sNewChar;
    if (     sClass.substr(-11) == 'collapsible')
    {
        sNewClass = sClass.substr(0, sClass.length - 11) + 'expandable';
        sNewChar  = '\u25B6'; /* black right-pointing triangle */
    }
    else if (sClass.substr(-10) == 'expandable')
    {
        sNewClass = sClass.substr(0, sClass.length - 10) + 'collapsible';
        sNewChar  = '\u25BC'; /* black down-pointing triangle */
    }
    else
    {
        console.log('toggleCollapsibleParent: Invalid class: ' + sClass);
        return true;
    }

    /* Update the parent (DT) class and anchor text. */
    oParent.className   = sNewClass;
    oAnchor.firstChild.textContent = sNewChar + oAnchor.firstChild.textContent.substr(1);

    /* Update the uncle (DD) class. */
    if (oDdElement)
        oDdElement.className = sNewClass;
    return true;
}

/**
 * Shows/hides a sub-category UL according to checkbox status.
 *
 * The checkbox is expected to be within a label element or something.
 *
 * @returns true
 * @param   oInput          The input checkbox.
 */
function toggleCollapsibleCheckbox(oInput)
{
    var oParent = oInput.parentElement;

    /* Find the UL sibling element. */
    var oUlElement = oParent.nextSibling;
    while (oUlElement != null && oUlElement.tagName != 'UL')
        oUlElement = oUlElement.nextSibling;

    /* Change the visibility. */
    if (oInput.checked)
        oUlElement.className = oUlElement.className.replace('expandable', 'collapsible');
    else
    {
        oUlElement.className = oUlElement.className.replace('collapsible', 'expandable');

        /* Make sure all sub-checkboxes are now unchecked. */
        var aoSubInputs = oUlElement.getElementsByTagName('input');
        var i;
        for (i = 0; i < aoSubInputs.length; i++)
            aoSubInputs[i].checked = false;
    }
    return true;
}

/**
 * Toggles the sidebar size so filters can more easily manipulated.
 */
function toggleSidebarSize()
{
    var sLinkText;
    if (document.body.className != 'tm-wide-side-menu')
    {
        document.body.className = 'tm-wide-side-menu';
        sLinkText = '\u00ab\u00ab';
    }
    else
    {
        document.body.className = '';
        sLinkText = '\u00bb\u00bb';
    }

    var aoToggleLink = document.getElementsByClassName('tm-sidebar-size-link');
    var i;
    for (i = 0; i < aoToggleLink.length; i++)
        if (   aoToggleLink[i].textContent.indexOf('\u00bb') >= 0
            || aoToggleLink[i].textContent.indexOf('\u00ab') >= 0)
            aoToggleLink[i].textContent = sLinkText;
}

/** @} */


/** @name Custom Tooltips
 * @{
 */

/** Enables non-iframe tooltip code. */
var g_fNewTooltips       = true;

/** Where we keep tooltip elements when not displayed. */
var g_dTooltips          = {};
var g_oCurrentTooltip    = null;
var g_idTooltipShowTimer = null;
var g_idTooltipHideTimer = null;
var g_cTooltipSvnRevisions = 12;

/**
 * Cancel showing/replacing/repositing a tooltip.
 */
function tooltipResetShowTimer()
{
    if (g_idTooltipShowTimer)
    {
        clearTimeout(g_idTooltipShowTimer);
        g_idTooltipShowTimer = null;
    }
}

/**
 * Cancel hiding of the current tooltip.
 */
function tooltipResetHideTimer()
{
    if (g_idTooltipHideTimer)
    {
        clearTimeout(g_idTooltipHideTimer);
        g_idTooltipHideTimer = null;
    }
}

/**
 * Really hide the tooltip.
 */
function tooltipReallyHide()
{
    if (g_oCurrentTooltip)
    {
        //console.log('tooltipReallyHide: ' + g_oCurrentTooltip);
        g_oCurrentTooltip.oElm.style.display = 'none';
        g_oCurrentTooltip = null;
    }
}

/**
 * Schedule the tooltip for hiding.
 */
function tooltipHide()
{
    function tooltipDelayedHide()
    {
        tooltipResetHideTimer();
        tooltipReallyHide();
    }

    /*
     * Cancel any pending show and schedule hiding if necessary.
     */
    tooltipResetShowTimer();
    if (g_oCurrentTooltip && !g_idTooltipHideTimer)
    {
        g_idTooltipHideTimer = setTimeout(tooltipDelayedHide, 700);
    }

    return true;
}

/**
 * Function that is repositions the tooltip when it's shown.
 *
 * Used directly, via onload, and hackish timers to catch all browsers and
 * whatnot.
 *
 * Will set several tooltip member variables related to position and space.
 */
function tooltipRepositionOnLoad()
{
    //console.log('tooltipRepositionOnLoad');
    if (g_oCurrentTooltip)
    {
        var oRelToRect = g_oCurrentTooltip.oRelToRect;
        var cxNeeded   = g_oCurrentTooltip.oElm.offsetWidth  + 8;
        var cyNeeded   = g_oCurrentTooltip.oElm.offsetHeight + 8;

        var cyWindow        = window.innerHeight;
        var yScroll         = window.pageYOffset || document.documentElement.scrollTop;
        var yScrollBottom   = yScroll + cyWindow;
        var cxWindow        = window.innerWidth;
        var xScroll         = window.pageXOffset || document.documentElement.scrollLeft;
        var xScrollRight    = xScroll + cxWindow;

        var cyAbove    = Math.max(oRelToRect.top, 0);
        var cyBelow    = Math.max(cyWindow - oRelToRect.bottom, 0);
        var cxLeft     = Math.max(oRelToRect.left, 0);
        var cxRight    = Math.max(cxWindow - oRelToRect.right, 0);

        var xPos;
        var yPos;

        //console.log('tooltipRepositionOnLoad: rect: x,y=' + oRelToRect.x + ',' + oRelToRect.y
        //            + ' cx,cy=' + oRelToRect.width + ',' + oRelToRect.height + ' top=' + oRelToRect.top
        //            + ' bottom=' + oRelToRect.bottom + ' left=' + oRelToRect.left + ' right=' + oRelToRect.right);
        //console.log('tooltipRepositionOnLoad: yScroll=' + yScroll + ' yScrollBottom=' + yScrollBottom);
        //console.log('tooltipRepositionOnLoad: cyAbove=' + cyAbove + ' cyBelow=' + cyBelow + ' cyNeeded=' + cyNeeded);
        //console.log('tooltipRepositionOnLoad: xScroll=' + xScroll + ' xScrollRight=' + xScrollRight);
        //console.log('tooltipRepositionOnLoad: cxLeft=' + cxLeft + ' cxRight=' + cxRight + ' cxNeeded=' + cxNeeded);

        /*
         * Decide where to put the thing.
         */
        if (cyNeeded < cyBelow)
        {
            yPos = yScroll + oRelToRect.top;
            g_oCurrentTooltip.cyMax = cyBelow;
            //console.log('tooltipRepositionOnLoad: #1');
        }
        else if (cyBelow >= cyAbove)
        {
            yPos = yScrollBottom - cyNeeded;
            g_oCurrentTooltip.cyMax = yScrollBottom - yPos;
            //console.log('tooltipRepositionOnLoad: #2');
        }
        else
        {
            yPos = yScroll + oRelToRect.bottom - cyNeeded;
            g_oCurrentTooltip.cyMax = yScrollBottom - yPos;
            //console.log('tooltipRepositionOnLoad: #3');
        }
        if (yPos < yScroll)
        {
            yPos = yScroll;
            g_oCurrentTooltip.cyMax = yScrollBottom - yPos;
            //console.log('tooltipRepositionOnLoad: #4');
        }
        g_oCurrentTooltip.yPos    = yPos;
        g_oCurrentTooltip.yScroll = yScroll;
        g_oCurrentTooltip.cyMaxUp = yPos - yScroll;
        //console.log('tooltipRepositionOnLoad: yPos=' + yPos + ' yScroll=' + yScroll + ' cyMaxUp=' + g_oCurrentTooltip.cyMaxUp);

        if (cxNeeded < cxRight)
        {
            xPos = xScroll + oRelToRect.right;
            g_oCurrentTooltip.cxMax = cxRight;
            //console.log('tooltipRepositionOnLoad: #5');
        }
        else
        {
            xPos = xScroll + oRelToRect.left - cxNeeded;
            if (xPos < xScroll)
                xPos = xScroll;
            g_oCurrentTooltip.cxMax = cxNeeded;
            //console.log('tooltipRepositionOnLoad: #6');
        }
        g_oCurrentTooltip.xPos    = xPos;
        g_oCurrentTooltip.xScroll = xScroll;
        //console.log('tooltipRepositionOnLoad: xPos=' + xPos + ' xScroll=' + xScroll);

        g_oCurrentTooltip.oElm.style.top  = yPos + 'px';
        g_oCurrentTooltip.oElm.style.left = xPos + 'px';
    }
    return true;
}


/**
 * Really show the tooltip.
 *
 * @param   oTooltip            The tooltip object.
 * @param   oRelTo              What to put the tooltip adjecent to.
 */
function tooltipReallyShow(oTooltip, oRelTo)
{
    var oRect;

    tooltipResetShowTimer();
    tooltipResetHideTimer();

    if (g_oCurrentTooltip == oTooltip)
    {
        //console.log('moving tooltip');
    }
    else if (g_oCurrentTooltip)
    {
        //console.log('removing current tooltip and showing new');
        tooltipReallyHide();
    }
    else
    {
        //console.log('showing tooltip');
    }

    //oTooltip.oElm.setAttribute('style', 'display: block; position: absolute;');
    oTooltip.oElm.style.position = 'absolute';
    oTooltip.oElm.style.display  = 'block';
    oRect = oRelTo.getBoundingClientRect();
    oTooltip.oRelToRect = oRect;

    g_oCurrentTooltip = oTooltip;

    /*
     * Do repositioning (again).
     */
    tooltipRepositionOnLoad();
}

/**
 * Tooltip onmouseenter handler .
 */
function tooltipElementOnMouseEnter()
{
    /*console.log('tooltipElementOnMouseEnter: arguments.length='+arguments.length+' [0]='+arguments[0]);
    console.log('ENT: currentTarget='+arguments[0].currentTarget+' id='+arguments[0].currentTarget.id+' class='+arguments[0].currentTarget.className); */
    tooltipResetShowTimer();
    tooltipResetHideTimer();
    return true;
}

/**
 * Tooltip onmouseout handler.
 *
 * @remarks We only use this and onmouseenter for one tooltip element (iframe
 *          for svn, because chrome is sending onmouseout events after
 *          onmouseneter for the next element, which would confuse this simple
 *          code.
 */
function tooltipElementOnMouseOut()
{
    var oEvt = arguments[0];
    /*console.log('tooltipElementOnMouseOut: arguments.length='+arguments.length+' [0]='+oEvt);
    console.log('OUT: currentTarget='+oEvt.currentTarget+' id='+oEvt.currentTarget.id+' class='+oEvt.currentTarget.className);*/

    /* Ignore the event if leaving to a child element. */
    var oElm = oEvt.toElement || oEvt.relatedTarget;
    if (oElm != this && oElm)
    {
        for (;;)
        {
            oElm = oElm.parentNode;
            if (!oElm || oElm == window)
                break;
            if (oElm == this)
            {
                console.log('OUT: was to child! - ignore');
                return false;
            }
        }
    }

    tooltipHide();
    return true;
}

/**
 * iframe.onload hook that repositions and resizes the tooltip.
 *
 * This is a little hacky and we're calling it one or three times too many to
 * work around various browser differences too.
 */
function svnHistoryTooltipOldOnLoad()
{
    //console.log('svnHistoryTooltipOldOnLoad');

    /*
     * Resize the tooltip to better fit the content.
     */
    tooltipRepositionOnLoad(); /* Sets cxMax and cyMax. */
    if (g_oCurrentTooltip && g_oCurrentTooltip.oIFrame.contentWindow)
    {
        var oIFrameElement = g_oCurrentTooltip.oIFrame;
        var cxSpace  = Math.max(oIFrameElement.offsetLeft * 2, 0); /* simplified */
        var cySpace  = Math.max(oIFrameElement.offsetTop  * 2, 0); /* simplified */
        var cxNeeded = oIFrameElement.contentWindow.document.body.scrollWidth  + cxSpace;
        var cyNeeded = oIFrameElement.contentWindow.document.body.scrollHeight + cySpace;
        var cx = Math.min(cxNeeded, g_oCurrentTooltip.cxMax);
        var cy;

        g_oCurrentTooltip.oElm.width = cx + 'px';
        oIFrameElement.width            = (cx - cxSpace) + 'px';
        if (cx >= cxNeeded)
        {
            //console.log('svnHistoryTooltipOldOnLoad: overflowX -> hidden');
            oIFrameElement.style.overflowX = 'hidden';
        }
        else
        {
            oIFrameElement.style.overflowX = 'scroll';
        }

        cy = Math.min(cyNeeded, g_oCurrentTooltip.cyMax);
        if (cyNeeded > g_oCurrentTooltip.cyMax && g_oCurrentTooltip.cyMaxUp > 0)
        {
            var cyMove = Math.min(cyNeeded - g_oCurrentTooltip.cyMax, g_oCurrentTooltip.cyMaxUp);
            g_oCurrentTooltip.cyMax += cyMove;
            g_oCurrentTooltip.yPos  -= cyMove;
            g_oCurrentTooltip.oElm.style.top = g_oCurrentTooltip.yPos + 'px';
            cy = Math.min(cyNeeded, g_oCurrentTooltip.cyMax);
        }

        g_oCurrentTooltip.oElm.height = cy + 'px';
        oIFrameElement.height            = (cy - cySpace) + 'px';
        if (cy >= cyNeeded)
        {
            //console.log('svnHistoryTooltipOldOnLoad: overflowY -> hidden');
            oIFrameElement.style.overflowY = 'hidden';
        }
        else
        {
            oIFrameElement.style.overflowY = 'scroll';
        }

        //console.log('cyNeeded='+cyNeeded+' cyMax='+g_oCurrentTooltip.cyMax+' cySpace='+cySpace+' cy='+cy);
        //console.log('oIFrameElement.offsetTop='+oIFrameElement.offsetTop);
        //console.log('svnHistoryTooltipOldOnLoad: cx='+cx+'cxMax='+g_oCurrentTooltip.cxMax+' cxNeeded='+cxNeeded+' cy='+cy+' cyMax='+g_oCurrentTooltip.cyMax);

        tooltipRepositionOnLoad();
    }
    return true;
}

/**
 * iframe.onload hook that repositions and resizes the tooltip.
 *
 * This is a little hacky and we're calling it one or three times too many to
 * work around various browser differences too.
 */
function svnHistoryTooltipNewOnLoad()
{
    //console.log('svnHistoryTooltipNewOnLoad');

    /*
     * Resize the tooltip to better fit the content.
     */
    tooltipRepositionOnLoad(); /* Sets cxMax and cyMax. */
    oTooltip = g_oCurrentTooltip;
    if (oTooltip)
    {
        var oElmInner = oTooltip.oInnerElm;
        var cxSpace  = Math.max(oElmInner.offsetLeft * 2, 0); /* simplified */
        var cySpace  = Math.max(oElmInner.offsetTop  * 2, 0); /* simplified */
        var cxNeeded = oElmInner.scrollWidth  + cxSpace;
        var cyNeeded = oElmInner.scrollHeight + cySpace;
        var cx = Math.min(cxNeeded, oTooltip.cxMax);

        oTooltip.oElm.width = cx + 'px';
        oElmInner.width     = (cx - cxSpace) + 'px';
        if (cx >= cxNeeded)
        {
            //console.log('svnHistoryTooltipNewOnLoad: overflowX -> hidden');
            oElmInner.style.overflowX = 'hidden';
        }
        else
        {
            oElmInner.style.overflowX = 'scroll';
        }

        var cy = Math.min(cyNeeded, oTooltip.cyMax);
        if (cyNeeded > oTooltip.cyMax && oTooltip.cyMaxUp > 0)
        {
            var cyMove = Math.min(cyNeeded - oTooltip.cyMax, oTooltip.cyMaxUp);
            oTooltip.cyMax += cyMove;
            oTooltip.yPos  -= cyMove;
            oTooltip.oElm.style.top = oTooltip.yPos + 'px';
            cy = Math.min(cyNeeded, oTooltip.cyMax);
        }

        oTooltip.oElm.height = cy + 'px';
        oElmInner.height     = (cy - cySpace) + 'px';
        if (cy >= cyNeeded)
        {
            //console.log('svnHistoryTooltipNewOnLoad: overflowY -> hidden');
            oElmInner.style.overflowY = 'hidden';
        }
        else
        {
            oElmInner.style.overflowY = 'scroll';
        }

        //console.log('cyNeeded='+cyNeeded+' cyMax='+oTooltip.cyMax+' cySpace='+cySpace+' cy='+cy);
        //console.log('oElmInner.offsetTop='+oElmInner.offsetTop);
        //console.log('svnHistoryTooltipNewOnLoad: cx='+cx+'cxMax='+oTooltip.cxMax+' cxNeeded='+cxNeeded+' cy='+cy+' cyMax='+oTooltip.cyMax);

        tooltipRepositionOnLoad();
    }
    return true;
}


function svnHistoryTooltipNewOnReadState(oTooltip, oRestReq, oParent)
{
    /*console.log('svnHistoryTooltipNewOnReadState: status=' + oRestReq.status + ' readyState=' + oRestReq.readyState);*/
    if (oRestReq.readyState != oRestReq.DONE)
    {
        oTooltip.oInnerElm.innerHTML = '<p>Loading ...(' + oRestReq.readyState + ')</p>';
        return true;
    }

    /*
     * Check the result and translate it to a javascript object (oResp).
     */
    var oResp = null;
    var sHtml;
    if (oRestReq.status != 200)
    {
        console.log('svnHistoryTooltipNewOnReadState: status=' + oRestReq.status);
        sHtml = '<p>error: status=' + oRestReq.status + '</p>';
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
     * Generate the HTML.
     *
     * Note! Make sure the highlighting code in svnHistoryTooltipNewDelayedShow
     *       continues to work after modifying this code.
     */
    if (oResp)
    {
        sHtml = '<div class="tmvcstimeline tmvcstimelinetooltip">\n';

        var aoCommits = oResp.aoCommits;
        var cCommits  = oResp.aoCommits.length;
        var iCurDay   = null;
        var i;
        for (i = 0; i < cCommits; i++)
        {
            var oCommit    = aoCommits[i];
            var tsCreated  = parseIsoTimestamp(oCommit.tsCreated);
            var iCommitDay = Math.floor((tsCreated.getTime() + tsCreated.getTimezoneOffset()) / (24 * 60 * 60 * 1000));
            if (iCurDay === null || iCurDay != iCommitDay)
            {
                if (iCurDay !== null)
                    sHtml += ' </dl>\n';
                iCurDay = iCommitDay;
                sHtml += ' <h2>' + tsCreated.toISOString().split('T')[0] + ' ' + g_kasDaysOfTheWeek[tsCreated.getDay()] + '</h2>\n';
                sHtml += ' <dl>\n';
            }
            Date

            var sHighligh = '';
            if (oCommit.iRevision == oTooltip.iRevision)
                sHighligh += ' class="tmvcstimeline-highlight"';

            sHtml += '  <dt id="r' + oCommit.iRevision + '"' + sHighligh + '>';
            sHtml += '<a href="' + oResp.sTracChangesetUrlFmt.replace('%(iRevision)s', oCommit.iRevision.toString());
            sHtml += '" target="_blank">';
            sHtml += '<span class="tmvcstimeline-time">' + escapeElem(formatTimeHHMM(tsCreated, true)) + '</span>'
            sHtml += ' Changeset <span class="tmvcstimeline-rev">[' + oCommit.iRevision + ']</span>';
            sHtml += ' by <span class="tmvcstimeline-author">' + escapeElem(oCommit.sAuthor) + '</span>';
            sHtml += '</a></dt>\n';
            sHtml += '  <dd' + sHighligh + '>' + escapeElem(oCommit.sMessage) + '</dd>\n';
        }

        if (iCurDay !== null)
            sHtml += ' </dl>\n';
        sHtml += '</div>';
    }

    /*console.log('svnHistoryTooltipNewOnReadState: sHtml=' + sHtml);*/
    oTooltip.oInnerElm.innerHTML = sHtml;

    tooltipReallyShow(oTooltip, oParent);
    svnHistoryTooltipNewOnLoad();
}

/**
 * Calculates the last revision to get when showing a tooltip for @a iRevision.
 *
 * A tooltip covers several change log entries, both to limit the number of
 * tooltips to load and to give context.  The exact number is defined by
 * g_cTooltipSvnRevisions.
 *
 * @returns Last revision in a tooltip.
 * @param   iRevision   The revision number.
 */
function svnHistoryTooltipCalcLastRevision(iRevision)
{
    var iFirstRev = Math.floor(iRevision / g_cTooltipSvnRevisions) * g_cTooltipSvnRevisions;
    return iFirstRev + g_cTooltipSvnRevisions - 1;
}

/**
 * Calculates a unique ID for the tooltip element.
 *
 * This is also used as dictionary index.
 *
 * @returns tooltip ID value (string).
 * @param   sRepository The repository name.
 * @param   iRevision   The revision number.
 */
function svnHistoryTooltipCalcId(sRepository, iRevision)
{
    return 'svnHistoryTooltip_' + sRepository + '_' + svnHistoryTooltipCalcLastRevision(iRevision);
}

/**
 * The onmouseenter event handler for creating the tooltip.
 *
 * @param   oEvt        The event.
 * @param   sRepository The repository name.
 * @param   iRevision   The revision number.
 * @param   sUrlPrefix  URL prefix for non-testmanager use.
 *
 * @remarks onmouseout must be set to call tooltipHide.
 */
function svnHistoryTooltipShowEx(oEvt, sRepository, iRevision, sUrlPrefix)
{
    var sKey    = svnHistoryTooltipCalcId(sRepository, iRevision);
    var oParent = oEvt.currentTarget;
    //console.log('svnHistoryTooltipShow ' + sRepository);

    function svnHistoryTooltipOldDelayedShow()
    {
        var sSrc;

        var oTooltip = g_dTooltips[sKey];
        //console.log('svnHistoryTooltipOldDelayedShow ' + sRepository + ' ' + oTooltip);
        if (!oTooltip)
        {
            /*
             * Create a new tooltip element.
             */
            //console.log('creating ' + sKey);
            oTooltip = {};
            oTooltip.oElm = document.createElement('div');
            oTooltip.oElm.setAttribute('id', sKey);
            oTooltip.oElm.className      = 'tmvcstooltip';
            //oTooltip.oElm.setAttribute('style', 'display:none; position: absolute;');
            oTooltip.oElm.style.display  = 'none';  /* Note! Must stay hidden till loaded, or parent jumps with #rXXXX.*/
            oTooltip.oElm.style.position = 'absolute';
            oTooltip.oElm.style.zIndex   = 6001;
            oTooltip.xPos      = 0;
            oTooltip.yPos      = 0;
            oTooltip.cxMax     = 0;
            oTooltip.cyMax     = 0;
            oTooltip.cyMaxUp   = 0;
            oTooltip.xScroll   = 0;
            oTooltip.yScroll   = 0;
            oTooltip.iRevision = iRevision;   /**< For  :target/highlighting */

            var oIFrameElement = document.createElement('iframe');
            oIFrameElement.setAttribute('id', sKey + '_iframe');
            oIFrameElement.style.position = 'relative';
            oIFrameElement.onmouseenter   = tooltipElementOnMouseEnter;
            //oIFrameElement.onmouseout     = tooltipElementOnMouseOut;
            oTooltip.oElm.appendChild(oIFrameElement);
            oTooltip.oIFrame = oIFrameElement;
            g_dTooltips[sKey] = oTooltip;

            document.body.appendChild(oTooltip.oElm);

            oIFrameElement.onload = function() { /* A slight delay here to give time for #rXXXX scrolling before we show it. */
                setTimeout(function(){
                                /*console.log('iframe/onload');*/
                                tooltipReallyShow(oTooltip, oParent);
                                svnHistoryTooltipOldOnLoad();
                           }, isBrowserInternetExplorer() ? 256 : 128);
            };

            var sUrl = sUrlPrefix + 'index.py?Action=VcsHistoryTooltip&repo=' + sRepository
                     + '&rev=' + svnHistoryTooltipCalcLastRevision(iRevision)
                     + '&cEntries=' + g_cTooltipSvnRevisions
                     + '#r' + iRevision;
            oIFrameElement.src = sUrl;
        }
        else
        {
            /*
             * Show the existing one, possibly with different :target/highlighting.
             */
            if (oTooltip.iRevision != iRevision)
            {
                //console.log('Changing revision ' + oTooltip.iRevision + ' -> ' + iRevision);
                oTooltip.oIFrame.contentWindow.location.hash = '#r' + iRevision;
                if (!isBrowserFirefox()) /* Chrome updates stuff like expected; Firefox OTOH doesn't change anything. */
                {
                    setTimeout(function() { /* Slight delay to make sure it scrolls before it's shown. */
                                   tooltipReallyShow(oTooltip, oParent);
                                   svnHistoryTooltipOldOnLoad();
                               }, isBrowserInternetExplorer() ? 256 : 64);
                }
                else
                    oTooltip.oIFrame.contentWindow.location.reload();
            }
            else
            {
                tooltipReallyShow(oTooltip, oParent);
                svnHistoryTooltipOldOnLoad();
            }
        }
    }

    function svnHistoryTooltipNewDelayedShow()
    {
        var sSrc;

        var oTooltip = g_dTooltips[sKey];
        /*console.log('svnHistoryTooltipNewDelayedShow: ' + sRepository + ' ' + oTooltip);*/
        if (!oTooltip)
        {
            /*
             * Create a new tooltip element.
             */
            /*console.log('creating ' + sKey);*/

            var oElm = document.createElement('div');
            oElm.setAttribute('id', sKey);
            oElm.className      = 'tmvcstooltipnew';
            //oElm.setAttribute('style', 'display:none; position: absolute;');
            oElm.style.display  = 'none';  /* Note! Must stay hidden till loaded, or parent jumps with #rXXXX.*/
            oElm.style.position = 'absolute';
            oElm.style.zIndex   = 6001;
            oElm.onmouseenter   = tooltipElementOnMouseEnter;
            oElm.onmouseout     = tooltipElementOnMouseOut;

            var oInnerElm = document.createElement('div');
            oInnerElm.className = 'tooltip-inner';
            oElm.appendChild(oInnerElm);

            oTooltip = {};
            oTooltip.oElm      = oElm;
            oTooltip.oInnerElm = oInnerElm;
            oTooltip.xPos      = 0;
            oTooltip.yPos      = 0;
            oTooltip.cxMax     = 0;
            oTooltip.cyMax     = 0;
            oTooltip.cyMaxUp   = 0;
            oTooltip.xScroll   = 0;
            oTooltip.yScroll   = 0;
            oTooltip.iRevision = iRevision;   /**< For  :target/highlighting */

            oRestReq = new XMLHttpRequest();
            oRestReq.onreadystatechange = function() { svnHistoryTooltipNewOnReadState(oTooltip, this, oParent); }
            oRestReq.open('GET', sUrlPrefix + 'rest.py?sPath=vcs/changelog/' + sRepository
                          + '/' + svnHistoryTooltipCalcLastRevision(iRevision) + '/' + g_cTooltipSvnRevisions);
            oRestReq.setRequestHeader('Content-type', 'application/json');

            document.body.appendChild(oTooltip.oElm);
            g_dTooltips[sKey] = oTooltip;

            oRestReq.send('');
        }
        else
        {
            /*
             * Show the existing one, possibly with different highlighting.
             * Note! Update this code when changing svnHistoryTooltipNewOnReadState.
             */
            if (oTooltip.iRevision != iRevision)
            {
                //console.log('Changing revision ' + oTooltip.iRevision + ' -> ' + iRevision);
                var oElmTimelineDiv = oTooltip.oInnerElm.firstElementChild;
                var i;
                for (i = 0; i < oElmTimelineDiv.children.length; i++)
                {
                    var oElm = oElmTimelineDiv.children[i];
                    //console.log('oElm='+oElm+' id='+oElm.id+' nodeName='+oElm.nodeName);
                    if (oElm.nodeName == 'DL')
                    {
                        var iCurRev = iRevision - 64;
                        var j;
                        for (j = 0; i < oElm.children.length; i++)
                        {
                            var oDlSubElm = oElm.children[i];
                            //console.log(' oDlSubElm='+oDlSubElm+' id='+oDlSubElm.id+' nodeName='+oDlSubElm.nodeName+' className='+oDlSubElm.className);
                            if (oDlSubElm.id.length > 2)
                                iCurRev = parseInt(oDlSubElm.id.substring(1), 10);
                            if (iCurRev == iRevision)
                                oDlSubElm.className = 'tmvcstimeline-highlight';
                            else
                                oDlSubElm.className = '';
                        }
                    }
                }
                oTooltip.iRevision = iRevision;
            }

            tooltipReallyShow(oTooltip, oParent);
            svnHistoryTooltipNewOnLoad();
        }
    }


    /*
     * Delay the change (in case the mouse moves on).
     */
    tooltipResetShowTimer();
    if (g_fNewTooltips)
        g_idTooltipShowTimer = setTimeout(svnHistoryTooltipNewDelayedShow, 512);
    else
        g_idTooltipShowTimer = setTimeout(svnHistoryTooltipOldDelayedShow, 512);
}

/**
 * The onmouseenter event handler for creating the tooltip.
 *
 * @param   oEvt        The event.
 * @param   sRepository The repository name.
 * @param   iRevision   The revision number.
 *
 * @remarks onmouseout must be set to call tooltipHide.
 */
function svnHistoryTooltipShow(oEvt, sRepository, iRevision)
{
    return svnHistoryTooltipShowEx(oEvt, sRepository, iRevision, '');
}

/** @} */


/** @name Debugging and Introspection
 * @{
 */

/**
 * Python-like dir() implementation.
 *
 * @returns Array of names associated with oObj.
 * @param   oObj        The object under inspection.  If not specified we'll
 *                      look at the window object.
 */
function pythonlikeDir(oObj, fDeep)
{
    var aRet = [];
    var dTmp = {};

    if (!oObj)
    {
        oObj = window;
    }

    for (var oCur = oObj; oCur; oCur = Object.getPrototypeOf(oCur))
    {
        var aThis = Object.getOwnPropertyNames(oCur);
        for (var i = 0; i < aThis.length; i++)
        {
            if (!(aThis[i] in dTmp))
            {
                dTmp[aThis[i]] = 1;
                aRet.push(aThis[i]);
            }
        }
    }

    return aRet;
}


/**
 * Python-like dir() implementation, shallow version.
 *
 * @returns Array of names associated with oObj.
 * @param   oObj        The object under inspection.  If not specified we'll
 *                      look at the window object.
 */
function pythonlikeShallowDir(oObj, fDeep)
{
    var aRet = [];
    var dTmp = {};

    if (oObj)
    {
        for (var i in oObj)
        {
            aRet.push(i);
        }
    }

    return aRet;
}



function dbgGetObjType(oObj)
{
    var sType = typeof oObj;
    if (sType == "object" && oObj !== null)
    {
        if (oObj.constructor && oObj.constructor.name)
        {
            sType = oObj.constructor.name;
        }
        else
        {
            var fnToString = Object.prototype.toString;
            var sTmp = fnToString.call(oObj);
            if (sTmp.indexOf('[object ') === 0)
            {
                sType = sTmp.substring(8, sTmp.length);
            }
        }
    }
    return sType;
}


/**
 * Dumps the given object to the console.
 *
 * @param   oObj        The object under inspection.
 * @param   sPrefix     What to prefix the log output with.
 */
function dbgDumpObj(oObj, sName, sPrefix)
{
    var aMembers;
    var sType;

    /*
     * Defaults
     */
    if (!oObj)
    {
        oObj = window;
    }

    if (!sPrefix)
    {
        if (sName)
        {
            sPrefix = sName + ':';
        }
        else
        {
            sPrefix = 'dbgDumpObj:';
        }
    }

    if (!sName)
    {
        sName = '';
    }

    /*
     * The object itself.
     */
    sPrefix = sPrefix + ' ';
    console.log(sPrefix + sName + ' ' + dbgGetObjType(oObj));

    /*
     * The members.
     */
    sPrefix = sPrefix + ' ';
    aMembers = pythonlikeDir(oObj);
    for (i = 0; i < aMembers.length; i++)
    {
        console.log(sPrefix + aMembers[i]);
    }

    return true;
}

function dbgDumpObjWorker(sType, sName, oObj, sPrefix)
{
    var sRet;
    switch (sType)
    {
        case 'function':
        {
            sRet = sPrefix + 'function ' + sName + '()' + '\n';
            break;
        }

        case 'object':
        {
            sRet = sPrefix + 'var ' + sName + '(' + dbgGetObjType(oObj) + ') =';
            if (oObj !== null)
            {
                sRet += '\n';
            }
            else
            {
                sRet += ' null\n';
            }
            break;
        }

        case 'string':
        {
            sRet = sPrefix + 'var ' + sName + '(string, ' + oObj.length + ')';
            if (oObj.length < 80)
            {
                sRet += ' = "' + oObj + '"\n';
            }
            else
            {
                sRet += '\n';
            }
            break;
        }

        case 'Oops!':
            sRet = sPrefix + sName + '(??)\n';
            break;

        default:
            sRet = sPrefix + 'var ' + sName + '(' + sType + ')\n';
            break;
    }
    return sRet;
}


function dbgObjInArray(aoObjs, oObj)
{
    var i = aoObjs.length;
    while (i > 0)
    {
        i--;
        if (aoObjs[i] === oObj)
        {
            return true;
        }
    }
    return false;
}

function dbgDumpObjTreeWorker(oObj, sPrefix, aParentObjs, cMaxDepth)
{
    var sRet     = '';
    var aMembers = pythonlikeShallowDir(oObj);
    var i;

    for (i = 0; i < aMembers.length; i++)
    {
        //var sName = i;
        var sName = aMembers[i];
        var oMember;
        var sType;
        var oEx;

        try
        {
            oMember = oObj[sName];
            sType = typeof oObj[sName];
        }
        catch (oEx)
        {
            oMember = null;
            sType = 'Oops!';
        }

        //sRet += '[' + i + '/' + aMembers.length + ']';
        sRet += dbgDumpObjWorker(sType, sName, oMember, sPrefix);

        if (   sType == 'object'
            && oObj !== null)
        {

            if (dbgObjInArray(aParentObjs, oMember))
            {
                sRet += sPrefix + '! parent recursion\n';
            }
            else if (   sName == 'previousSibling'
                     || sName == 'previousElement'
                     || sName == 'lastChild'
                     || sName == 'firstElementChild'
                     || sName == 'lastElementChild'
                     || sName == 'nextElementSibling'
                     || sName == 'prevElementSibling'
                     || sName == 'parentElement'
                     || sName == 'ownerDocument')
            {
                sRet += sPrefix + '! potentially dangerous element name\n';
            }
            else if (aParentObjs.length >= cMaxDepth)
            {
                sRet = sRet.substring(0, sRet.length - 1);
                sRet += ' <too deep>!\n';
            }
            else
            {

                aParentObjs.push(oMember);
                if (i + 1 < aMembers.length)
                {
                    sRet += dbgDumpObjTreeWorker(oMember, sPrefix + '| ', aParentObjs, cMaxDepth);
                }
                else
                {
                    sRet += dbgDumpObjTreeWorker(oMember, sPrefix.substring(0, sPrefix.length - 2) + '  | ', aParentObjs, cMaxDepth);
                }
                aParentObjs.pop();
            }
        }
    }
    return sRet;
}

/**
 * Dumps the given object and all it's subobjects to the console.
 *
 * @returns String dump of the object.
 * @param   oObj        The object under inspection.
 * @param   sName       The object name (optional).
 * @param   sPrefix     What to prefix the log output with (optional).
 * @param   cMaxDepth   The max depth, optional.
 */
function dbgDumpObjTree(oObj, sName, sPrefix, cMaxDepth)
{
    var sType;
    var sRet;
    var oEx;

    /*
     * Defaults
     */
    if (!sPrefix)
    {
        sPrefix = '';
    }

    if (!sName)
    {
        sName = '??';
    }

    if (!cMaxDepth)
    {
        cMaxDepth = 2;
    }

    /*
     * The object itself.
     */
    try
    {
        sType = typeof oObj;
    }
    catch (oEx)
    {
        sType = 'Oops!';
    }
    sRet = dbgDumpObjWorker(sType, sName, oObj, sPrefix);
    if (sType == 'object' && oObj !== null)
    {
        var aParentObjs = Array();
        aParentObjs.push(oObj);
        sRet += dbgDumpObjTreeWorker(oObj, sPrefix + '| ', aParentObjs, cMaxDepth);
    }

    return sRet;
}

function dbgLogString(sLongString)
{
    var aStrings = sLongString.split("\n");
    var i;
    for (i = 0; i < aStrings.length; i++)
    {
        console.log(aStrings[i]);
    }
    console.log('dbgLogString - end - ' + aStrings.length + '/' + sLongString.length);
    return true;
}

function dbgLogObjTree(oObj, sName, sPrefix, cMaxDepth)
{
    return dbgLogString(dbgDumpObjTree(oObj, sName, sPrefix, cMaxDepth));
}

/** @}  */

