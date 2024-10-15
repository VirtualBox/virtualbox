<?php
/* $Id: clienttest.php 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*!file
 * Sample client for the VirtualBox webservice, written in PHP.
 *
 * Run the VirtualBox web service server first; see the VirtualBox
 * SDK reference for details.
 *
 * The following license applies to this file only:
 */

/*
 * Contributed by James Lucas (mjlucas at eng.uts.edu.au).
 *
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

require_once('./vboxServiceWrappers.php');

//Connect to webservice
$connection = new SoapClient("vboxwebService.wsdl", array('location' => "http://localhost:18083/"));

//Logon to webservice
$websessionManager = new IWebsessionManager($connection);
// Dummy username and password (change to appropriate values or set authentication method to null)
$virtualbox = $websessionManager->logon("username","password");

//Get a list of registered machines
$machines = $virtualbox->machines;

//Take a screenshot of the first vm we find that is running
foreach ($machines as $machine)
{
    if ( 'Running' == $machine->state )
    {
        $session = $websessionManager->getSessionObject($virtualbox->handle);
        $uuid = $machine->id;
        $machine->lockMachine($session->handle, "Shared");
        try
        {
            $console = $session->console;
            $display = $console->display;
            list($screenWidth, $screenHeight, $screenBpp, $screenX, $screenY, $screenStatus) = $display->getScreenResolution(0 /* First screen */);

            $imageraw = $display->takeScreenShotToArray(0 /* First screen */, $screenWidth, $screenHeight, "RGBA");
            echo "Screenshot size: " . sizeof($imageraw) . "\n";

            $filename = 'screenshot.png';
            echo "Saving screenshot of " . $machine->name . " (${screenWidth}x${screenHeight}, ${screenBpp}BPP) to $filename\n";
            $image = imagecreatetruecolor($screenWidth, $screenHeight);

            for ($height = 0; $height < $screenHeight; $height++)
            {
                for ($width = 0; $width < $screenWidth; $width++)
                {
                    $start = ($height*$screenWidth + $width)*4;
                    $red = $imageraw[$start];
                    $green = $imageraw[($start+1)];
                    $blue = $imageraw[($start+2)];
                    //$alpha = $image[$start+3];

                    $colour = imagecolorallocate($image, $red, $green, $blue);

                    imagesetpixel($image, $width, $height, $colour);
                }
            }

            imagepng($image, $filename);
        }
        catch (Exception $ex)
        {
            echo $ex->getMessage();
        }

        $session->unlockMachine();

        $machine->releaseRemote();
        $session->releaseRemote();

        break;
    }
}

$websessionManager->logoff($virtualbox->handle);

?>

