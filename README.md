# kernel-lts

OpenELA provides a platform for developers to collaborate on maintaining kernels beyond 
the commitment of upstream maintainers. As enterprise distributions, we often are tasked 
to keep software viable even after community support has ended, and we believe 
collaboration is the best way to maintain these kernels. 

This repository contains a continuation of stable kernel releases from upstream stable. This continuation follows all the upstream [stable rules](https://www.kernel.org/doc/html/latest/process/stable-kernel-rules.html), does not target specific hardware, vendors, or users, and the patches are primarily selected from ongoing upstream stable kernels.

We appreciate the work of the upstream stable maintainers, and send a big thank you to Greg Kroah-Hartman and Sasha Levin. This project is not affiliated with them, the Linux Kernel Stable project, or upstream stable maintainers. Please address any issues with the OpenELA LTS kernels to kernel-lts@openela.org

# Current Releases

| Kernel base version | Last upstream version | Upstream EOL      | Extension EOL |
| ------------------- | --------------------- | ----------------- | ------------- |
| 4.14                | 4.14.336              | January 2024 [^2] | December 2024 |

# Getting Updates about New Kernel Releases

[Subscribe to kernel-lts@openela.org](https://groups.google.com/a/openela.org/g/kernel-lts) for updates to kernels 

# Contributing to kernel-lts

Most patches for the LTS maintained stable trees here will be automatically pulled into the OpenELA LTS branches from the upstream stable branches.
However, there will be cases where patches do not apply cleanly and require developer intervention to apply. In general this will be handled 
by the OpenELA kernel-lts maintainers, however a digest of non-applied patches will accompany each release of the LTS kernel, in mbox format,
at https://github.com/openela/kernel-lts/releases .
Developers are encouraged to help get patches committed, and to submit those patches to the mailing list kernel-lts@openela.org or via github issues.

The OpenELA stable kernels do not accept patches which are not part of the mainline Linux kernel or which are not already present in stable kernels that upstream supports. Please submit those patches directly to mainline Linux or stable kernels. Refer to the upstream Stable Kernel Rules[^1] for additional guidance. Patches which are not applicable to upstream Linux are considered out-of-scope for this project and should be maintained via vendor or distribution specific customization.

## Tagging

To avoid confusion with the upstream stable releases, kernels will be tagged with the following format: x.y.z-openela (i.e. `4.14.<n>-openela`)

## Reporting Issues

Issues may be reported in this github repo via github issues or to the mailing list kernel-lts@openela.org; please do not contact stable-at-kernel.org or any upstream maintainer with questions about this repository.

## Testing 

We encourage testing and reporting of bugs against these kernels. A proper bug report should demonstrate that the upstream stable kernels are _not_ affected by the vulnerability -- otherwise, we would encourage you to file the bug report (or better yet, write a patch) for the upstream stable kernel or the mainline Linux kernel. For example, in the OpenELA maintained 4.14 branch, a bug which also exists in 4.19 should be fixed in the 4.19 stable tree (and automatically backported into the OpenELA maintained branch).





[^1]: <https://docs.kernel.org/process/stable-kernel-rules.html>
[^2]: <https://lore.kernel.org/stable/2024011046-ecology-tiptoeing-ce50@gregkh/>


