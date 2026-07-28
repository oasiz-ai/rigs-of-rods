/// \title deterministic visual capture smoke
/// \brief Warms a scene, requests one full-frame screenshot, and exits cleanly.
///
/// Run explicitly with
/// `RoR -map <terrain>.terrn2 -runscript example_ci_visual_capture.as`.
/// The script is inert during ordinary game launches.

uint gCiVisualCaptureFrame = 0;

void main()
{
    game.log("[RoR|CI|VisualCapture] START");
}

void frameStep(float dt)
{
    // Frame-count timing makes the capture independent of host wall-clock
    // speed. The screenshot handler hides the cursor before readback.
    gCiVisualCaptureFrame++;
    if (gCiVisualCaptureFrame == 120)
    {
        game.log("[RoR|CI|VisualCapture] CAPTURE frame=120");
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
    }
    else if (gCiVisualCaptureFrame == 180)
    {
        game.log("[RoR|CI|VisualCapture] PASS frames=180 captures=1");
        game.quitGame();
    }
}
