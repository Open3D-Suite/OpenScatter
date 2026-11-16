-- SPDX-License-Identifier: GPL-3.0-or-later
-- Plugin test: is the instance buffer the same every time?
--
-- The runner starts this script twice, in two processes (environment variable OSC_TEST_RUN = 1, 2),
-- and compares the "HASH" lines of the two result files. Inside one process the same scene is also
-- scattered with 1, 3 and every hardware thread, and built a second time from scratch.
-- Result: results\determinism_run1.txt and determinism_run2.txt; the runner writes determinism.txt.

fileIn ((getFilenamePath (getSourceFileName())) + "common.ms")

(
	local run = systemTools.getEnvVariable "OSC_TEST_RUN"
	if run == undefined do run = "1"
	OSC_tOpen ("determinism_run" + run)
	try
	(
		local hashes = #()
		local counts = #()
		for pass = 1 to 2 do
		(
			local sc = OSC_tBuildScene()
			for threads in #(0, 1, 3) do
			(
				sc.threads = threads
				-- the thread count is an input like any other: it forces a new scatter
				append hashes (sc.getInstanceHash())
				append counts (sc.getInstanceCount())
				OSC_tWrite ("  pass " + pass as string + ", threads " + threads as string + ": " + counts[counts.count] as string + \
					" instances, hash " + hashes[hashes.count] + "   " + sc.getStatistics())
			)
		)
		local same = true
		for h in hashes do (if h != hashes[1] do same = false)
		OSC_tCheck "the same hash with 1, 3 and all threads, and when the scene is built again" same detail:(hashes as string)
		OSC_tCheck "there are instances to compare" (counts[1] > 100) detail:(counts[1] as string)

		-- a second, larger scatter: a hundred thousand instances and more
		local sc = OSC_tBuildScene density:5000.0
		local bigHash = sc.getInstanceHash()
		local bigCount = sc.getInstanceCount()
		sc.threads = 1
		OSC_tCheck "a large scatter: the same hash with one thread and with all" (sc.getInstanceHash() == bigHash and bigCount > 50000) \
			detail:(bigCount as string + " instances, hash " + bigHash)
		OSC_tWrite ("HASH small " + hashes[1] + " " + counts[1] as string)
		OSC_tWrite ("HASH large " + bigHash + " " + bigCount as string)
	)
	catch (OSC_tCheck "no exception" false detail:(getCurrentException()))
	OSC_tClose()
)
