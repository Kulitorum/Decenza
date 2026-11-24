// Reusable Qt Wasm module artifacts that a release run may trust, newest first.
// Shared by release.yml's "Find a reusable…" and "Keep the two newest…" steps (actions/github-script).
//
// Listing artifacts by name is one API call. The search it replaces walked the last 100
// release runs and their jobs, and took 124 s (run 45439543153) on the Wasm job's critical path.
module.exports = async function trustedArtifacts({ github, context }, name) {
  const repository = await github.rest.repos.get(context.repo);
  const trustedBranches = new Set([
    context.ref.startsWith('refs/heads/') ? context.ref.slice('refs/heads/'.length) : null,
    repository.data.default_branch,
  ].filter(Boolean));

  const artifacts = (await github.paginate(github.rest.actions.listArtifactsForRepo, {
    ...context.repo, name, per_page: 100,
  }))
    .filter(a => a.name === name && !a.expired && a.workflow_run?.id)
    .sort((a, b) => Date.parse(b.created_at) - Date.parse(a.created_at));

  const trusted = [];
  for (const artifact of artifacts) {
    const runId = artifact.workflow_run.id;
    // This run's own upload: its Wasm job has not concluded yet.
    if (runId === context.runId) {
      trusted.push({ artifact, runId });
      continue;
    }
    const { data: run } = await github.rest.actions.getWorkflowRun({ ...context.repo, run_id: runId });
    const sameRepository = run.head_repository?.id === repository.data.id;
    const trustedBranchDispatch = run.event === 'workflow_dispatch' && trustedBranches.has(run.head_branch);
    // A dispatch on a tag is how a pre-release is rebuilt when HEAD is a [skip ci] commit (CI_CD.md).
    const trustedTag = ['push', 'workflow_dispatch'].includes(run.event) && /^v\d/.test(run.head_branch || '');
    const workflowPath = (run.path || '').split('@')[0].replace(/^\//, '');
    if (!sameRepository || (!trustedBranchDispatch && !trustedTag)
        || workflowPath !== '.github/workflows/release.yml') continue;

    const jobs = await github.paginate(github.rest.actions.listJobsForWorkflowRun, {
      ...context.repo, run_id: runId, filter: 'all', per_page: 100,
    });
    const wasmJob = jobs
      .filter(job => job.name === 'Wasm bundle')
      .sort((a, b) => b.id - a.id)[0];
    if (wasmJob?.conclusion === 'success') trusted.push({ artifact, runId, wasmJobId: wasmJob.id });
  }
  return trusted;
};
