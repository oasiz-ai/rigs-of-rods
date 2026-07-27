/// \title application-bundle smoke test
/// \brief Exercises normal startup, rendered frames, and clean shutdown.
///
/// Run explicitly with `RoR -runscript example_ci_bundle_smoke.as`.
/// The script is inert during ordinary game launches.

uint gCiBundleSmokeFrame = 0;

void main()
{
    game.log("[RoR|CI|BundleSmoke] START");
}

void frameStep(float dt)
{
    // Ten callbacks prove that startup reached the real render loop. Use a
    // frame count instead of elapsed wall time so slow CI hosts remain valid.
    gCiBundleSmokeFrame++;
    if (gCiBundleSmokeFrame == 10)
    {
        game.log("[RoR|CI|BundleSmoke] PASS frames=10");
        game.quitGame();
    }
}
