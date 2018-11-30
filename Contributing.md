# Contributing

* This project is open to contributions of bug fixes and support for new hardware and will consider larger feature contributions.
  * Gator is primarily developed internally to Arm alongside the Streamline product. This means that the [gator GitHub repository] is only used for publishing releases.
  * Because gator is maintained internally to Arm, the logistics of integrating larger features and the need to align with the Streamline road map mean you are recommended to discuss new feature contributions with the maintainers first. To do so please open an issue on the project with your feature proposal.
  * New / large feature contributions may also require appropriate test coverage. The maintainers will discuss these requirements with you as part of the discussion around your feature proposal.
* Contributions are made under the appropriate license for the associated sub-directory (GPL-2.0-only or BSD-3-Clause in the case of `annotate`).
* Contributions consisting of changes to any imported third party IP must additionally comply with any and all terms of the affected third party IP.

## Making a contribution

### Getting started

* Make sure you have a GitHub account.
* Create an issue for your work if one does not already exist. This gives everyone visibility of whether others are working on something similar.
* Fork gator on GitHub.
* Clone the fork to your own machine.
* Create a local topic branch based on the gator `master` branch.

### Making changes

* Make commits of logical units. See these general Git guidelines for contributing to a project.
* Keep the commits on topic. If you need to fix another bug or make another enhancement, please create a separate issue and address it on a separate topic branch.
* Avoid long commit series. If you do have a long series, consider whether some commits should be squashed together or addressed in a separate topic.
* Make sure your commit messages are in the proper format. If a commit fixes a GitHub issue, include a reference (e.g. "fixes ARM-software/gator#45"); this ensures the issue is automatically closed when merged into the gator `master` branch.
* Where appropriate, please update the documentation.
* Ensure that each changed file has the correct copyright and license information. Files that entirely consist of contributions to this project should have the copyright notice and the GPL-2.0-only or BSD-3-Clause SPDX license identifier. Files that contain changes to imported Third Party IP should contain a notice as follows, with the original copyright and license text retained:
  ``Portions copyright (c) [XXXX-]YYYY, Arm Limited and Contributors. All rights reserved.``
  where XXXX is the year of first contribution (if different to YYYY) and YYYY is the year of most recent contribution.
* If not done previously, you may add your name or your company name to the [Acknowledgements] file.
* Arm engineers remain primary maintainers of this project, however:
  * If you are making some form of target / device specific contribution, and particularly if you are submitting that contribution on behalf of the originator / manufacturer of that device, please update the [Maintainers] file adding an appropriate contact as sub-maintainer for that contribution.
  * If you are submitting new files that you intend to be the technical sub-maintainer for, then also update the [Maintainers] file.
* Please test your changes. Gator supports data collection via `gator.ko` and via Linux perf API. Contributions of new counters are generally expected to support both collection methods. Please ensure that they do.

### Submitting changes

* Ensure that each commit in the series has at least one `Signed-off-by:` line, using your real name and email address.
  The names in the `Signed-off-by:` and `Author:` lines must match.
  If anyone else contributes to the commit, they must also add their own `Signed-off-by:` line.
  By adding this line the contributor certifies the contribution is made under the terms of the [Developer Certificate of Origin (DCO)].
* Push your local changes to your fork of the repository.
* Submit a pull request to the gator `mater` branch.
* The changes in the pull request will then undergo further review and testing by the maintainers. Any review comments will be made as comments on the pull request. This may require you to do some rework.
* When the changes are accepted, the maintainers will integrate them.
  * Typically, the maintainers will attempt to merge your changes against the Arm internal repository. If they are successful, we will also merge them against the public `master` branch on GitHub.
  * If the changes cannot be merged cleanly onto the Arm internal repository without significant rework, we may ask you to rebase and resubmit your pull request once those changes have been published to GitHub.
  * If the pull request is not based on a recent commit, the maintainers may rebase it onto the `master` branch first, or ask you to do this.
  * If the pull request cannot be automatically merged, the maintainers will ask you to rebase it onto the `master` branch.
  * Please do not delete your topic branch until it is safely merged into the `master` branch.
* Please avoid creating merge commits in the pull request itself.

[gator GitHub repository]: https://github.com/ARM-software/gator/
[Acknowledgements]: Acknowledgements.md
[Maintainers]: Maintainers.md
[Developer Certificate of Origin (DCO)]: dco.txt
