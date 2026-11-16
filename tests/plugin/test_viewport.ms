-- SPDX-License-Identifier: GPL-3.0-or-later
-- Plugin test: are the instances drawn in the viewport, in shaded and in wireframe views, as boxes
-- and as meshes, within the budget of the display driver?
--
-- Needs a 3ds Max WITH its interface (there is no viewport in 3dsmaxbatch): run_viewport_test.ps1
-- starts a second, fresh one with the driver it is told, runs this script in it and waits for it to
-- quit. Never run it in a 3ds Max with real work in it: it resets the scene and quits when done.
--
-- For every case the view is redrawn completely and read back; the share of the view that differs
-- from the empty scene says whether anything was drawn, and the object's own report says how much it
-- sent to the graphics card. Result: results\viewport_<driver>.txt, captures in results\images\.

fileIn ((getFilenamePath (getSourceFileName())) + "common.ms")

-- Points the active perspective view at the origin from above.
global OSC_tLook
fn OSC_tLook dist =
(
	local p = [0, -dist * 0.8, dist * 0.6]
	local z = normalize p
	local x = normalize (cross [0, 0, 1] z)
	local y = cross z x
	viewport.setTM (inverse (matrix3 x y z p))
)

-- Redraws, reads the view back and returns #(image, ms for the redraw and the read-back).
global OSC_tGrab
fn OSC_tGrab =
(
	local t0 = timeStamp()
	completeRedraw()
	local img = gw.getViewportDib()
	#(img, timeStamp() - t0)
)

-- The share (in percent) of sampled pixels that differ between two captures of the same size.
global OSC_tChanged
fn OSC_tChanged a b =
(
	local hit = 0
	local total = 0
	for y = 0 to (a.height - 1) by 4 do
	(
		local ra = getPixels a [0, y] a.width
		local rb = getPixels b [0, y] b.width
		for x = 1 to ra.count by 4 do
		(
			total += 1
			if (abs (ra[x].r - rb[x].r) + abs (ra[x].g - rb[x].g) + abs (ra[x].b - rb[x].b)) > 24 do hit += 1
		)
	)
	if total > 0 then (100.0 * hit / total) else 0.0
)

(
	local tag = systemTools.getEnvVariable "OSC_TEST_DRIVER"
	if tag == undefined do tag = "default"
	OSC_tOpen ("viewport_" + tag)
	try
	(
		OSC_tWrite ("3ds Max " + (maxVersion()) as string + "   settings file: " + getMAXIniFile())
		local sc = OSC_tBuildScene density:2000.0
		OSC_tControl.isHidden = true
		for m in OSC_tModels do m.isHidden = true
		OSC_tSurface.isHidden = true
		sc.viewportMode = 0
		sc.iconSize = 0
		viewport.setLayout #layout_1
		viewport.setType #view_persp_user
		viewport.SetRenderLevel #smoothhighlights
		OSC_tLook 330
		local n = sc.getInstanceCount()
		OSC_tWrite ("instances: " + n as string + "   view size: " + (getViewSize()) as string)

		local empty = (OSC_tGrab())[1]
		local base = 0
		for i = 1 to 3 do base = (OSC_tGrab())[2]
		OSC_tWrite ("empty view: " + base as string + " ms to redraw and read back")
		makeDir (OSC_tResults + "\\images") all:true

		-- name, viewport mode, wireframe?, least share of the view that must change
		local cases = #(#("boxes, shaded", 1, false, 5.0), #("boxes, wireframe", 1, true, 2.0),
		                #("meshes, shaded", 2, false, 5.0), #("meshes, wireframe", 2, true, 2.0))
		local software = false
		for cs in cases do
		(
			viewport.SetRenderLevel (if cs[3] then #wireframe else #smoothhighlights)
			sc.viewportMode = 0
			local without = (OSC_tGrab())[1]
			sc.viewportMode = cs[2]
			local first = OSC_tGrab()
			local grab = OSC_tGrab()
			local share = OSC_tChanged without grab[1]
			local report = sc.getViewportReport()
			software = (OSC_tField report "software") == 1
			grab[1].filename = OSC_tResults + "\\images\\viewport_" + tag + "_" + (substituteString (substituteString cs[1] ", " "_") " " "_") + ".png"
			save grab[1]
			OSC_tCheck (cs[1] + ": the instances are drawn") (share >= cs[4] and (OSC_tField report "instances") > 0 and (OSC_tField report "refused") == 0) \
				detail:((formattedPrint share format:".1f") + " % of the view changed; first redraw " + first[2] as string + " ms, then " + \
				grab[2] as string + " ms (empty view " + base as string + " ms)   " + report)
		)
		viewport.SetRenderLevel #smoothhighlights

		-- the budget
		local report = sc.getViewportReport()
		OSC_tWrite ("driver: " + report)
		local budget = OSC_tField report "budgetInstances"
		OSC_tCheck "the display driver is known" ((findString report "driver=unknown") == undefined) detail:report
		if software then OSC_tCheck "software driver: the small budget" (budget <= 50000) detail:(budget as string + " instances")
		else OSC_tCheck "GPU driver: the large budget" (budget >= 1000000) detail:(budget as string + " instances")

		-- more instances than the budget: thinned, not refused
		sc.viewportMode = 1
		sc.viewportMaxInstances = 5000
		completeRedraw()
		report = sc.getViewportReport()
		OSC_tCheck "a limit of 5 000 instances thins the display evenly" ((OSC_tField report "instances") <= 5000 and (OSC_tField report "instances") > 3000) detail:report
		local thinned = (OSC_tGrab())[1]
		thinned.filename = OSC_tResults + "\\images\\viewport_" + tag + "_boxes_thinned.png"
		save thinned
		sc.viewportMaxInstances = 0

		-- meshes over the triangle budget fall back to boxes
		sc.viewportMode = 2
		sc.viewportMaxTriangles = 200000
		completeRedraw()
		report = sc.getViewportReport()
		OSC_tCheck "meshes over the triangle budget are drawn as boxes" ((findString report "drawn=boxes") != undefined and (OSC_tField report "triangles") <= 200000) detail:report
		sc.viewportMaxTriangles = 0

		-- the icon
		sc.viewportMode = 0
		local noIcon = (OSC_tGrab())[1]
		sc.iconSize = 120
		local icon = (OSC_tGrab())[1]
		OSC_tCheck "the icon is drawn" ((OSC_tChanged noIcon icon) > 0.0) detail:((formattedPrint (OSC_tChanged noIcon icon) format:".2f") + " % of the view changed")

		-- a large scatter, with the budget the driver gets
		sc.iconSize = 0
		sc.viewportMode = 1
		sc.models = #(OSC_tModels[1])
		sc.modelProbabilities = #(1.0)
		sc.scaleMin = [0.02, 0.02, 0.02]
		sc.scaleMax = [0.03, 0.03, 0.03]
		sc.density = 40000.0
		local big = sc.getInstanceCount()
		local first = OSC_tGrab()
		local ms = 0
		for i = 1 to 3 do ms = (OSC_tGrab())[2]
		OSC_tWrite ("  info  " + big as string + " instances as boxes: first redraw " + first[2] as string + " ms, then " + ms as string + \
			" ms (empty view " + base as string + " ms)   " + sc.getViewportReport())
	)
	catch (OSC_tCheck "no exception" false detail:(getCurrentException()))
	OSC_tClose()
)
quitMax #noPrompt
