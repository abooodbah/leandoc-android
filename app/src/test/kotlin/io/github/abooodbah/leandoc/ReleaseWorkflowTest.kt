package io.github.abooodbah.leandoc

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The release workflow records a build provenance attestation for the release
 * APK and bundle, after they are built and their signer is checked and before
 * they are attached to the release. Read as text: the workflow runs only for a
 * tag, so this is the check a pull request can make.
 */
class ReleaseWorkflowTest {
    private val workflow = File("../.github/workflows/release.yml").readText()

    /** Step names in order. */
    private val steps = Regex("""^      - name: (.+)$""", RegexOption.MULTILINE)
        .findAll(workflow).map { it.groupValues[1].trim() }.toList()

    private val attestStep: String
        get() {
            val start = workflow.indexOf("      - name: Attest build provenance")
            assertTrue("an attestation step", start >= 0)
            val end = workflow.indexOf("\n      - name:", start + 1).takeIf { it > 0 } ?: workflow.length
            return workflow.substring(start, end)
        }

    @Test
    fun theReleaseJobMaySignAnAttestation() {
        val job = workflow.substringAfter("\n  files:\n").substringBefore("\n    steps:\n")
        for (line in listOf("contents: write", "id-token: write", "attestations: write")) {
            assertTrue("the release job has $line", Regex("""^      ${Regex.escape(line)}$""", RegexOption.MULTILINE).containsMatchIn(job))
        }
    }

    @Test
    fun theAttestationActionIsPinnedToItsCommit() {
        assertTrue(
            attestStep,
            attestStep.contains("uses: actions/attest-build-provenance@4d101475d8b20a2381f78447822ac1eab6504dd8 # v4.2.2"),
        )
    }

    @Test
    fun theReleaseApkAndBundleAreAttested() {
        val subjects = attestStep.substringAfter("subject-path: |\n").lines()
            .map { it.trim() }.filter { it.isNotEmpty() }
        assertEquals(
            listOf(
                "dist/LeanDoc-\${{ steps.collect.outputs.version }}.apk",
                "dist/LeanDoc-\${{ steps.collect.outputs.version }}.aab",
            ),
            subjects,
        )
        assertTrue("the collect step sets the version", workflow.contains("""echo "version=${'$'}version" >> "${'$'}GITHUB_OUTPUT""""))
    }

    @Test
    fun theFilesAreAttestedAfterTheirChecksAndBeforeTheRelease() {
        val attest = steps.indexOfFirst { it.startsWith("Attest build provenance") }
        assertTrue(steps.toString(), attest >= 0)
        assertTrue("after the signer check", steps.indexOf("Check the signer") in 0 until attest)
        assertTrue("after the files are named and hashed", steps.indexOf("Name, collect and hash") in 0 until attest)
        assertTrue("before they are attached", attest < steps.indexOf("Attach to the release"))
    }
}
