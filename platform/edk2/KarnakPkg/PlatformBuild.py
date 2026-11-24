#!/usr/bin/env python3
#
# Amazon Fire HD 8 (2018) "karnak" platform build.
#
# Modeled on edk2-mtk/Platform/Xiaomi/emeraldPkg/PlatformBuild.py.
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
import logging
import os
import subprocess

from edk2toolext import edk2_logging
from edk2toolext.environment import shell_environment
from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_pr_eval import PrEvalSettingsManager
from edk2toolext.invocables.edk2_setup import (RequiredSubmodule, SetupSettingsManager)
from edk2toolext.invocables.edk2_update import UpdateSettingsManager
from edk2toolext.invocables.edk2_parse import ParseSettingsManager

# Values must match include/karnak/hardware_map.h.
FD_BASE = "0x50000000"
FD_SIZE = "0x00700000"
DTB_STORE = "0x4FE00000"
DTB_PTR_STORE = "0x4FF00000"
DTB_MAX = "0x00100000"

BOOTSHIM_DIR = os.path.join("Platform", "Amazon", "KarnakPkg", "BootShim")


class CommonPlatform(object):
    PackagesSupported = ("KarnakPkg",)
    ArchSupported = ("AARCH64",)
    TargetsSupported = ("DEBUG", "RELEASE")
    Scopes = ("t1100", "gcc_aarch64_linux", "edk2-build")
    WorkspaceRoot = os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    )
    PackagesPath = (
        "Platform/Amazon",
        "Common/edk2",
        "Common/edk2-platforms",
        "Silicon/MediaTek",
    )


class SettingsManager(UpdateSettingsManager, SetupSettingsManager,
                      PrEvalSettingsManager, ParseSettingsManager):
    def GetPackagesSupported(self):
        return CommonPlatform.PackagesSupported

    def GetArchitecturesSupported(self):
        return CommonPlatform.ArchSupported

    def GetTargetsSupported(self):
        return CommonPlatform.TargetsSupported

    def GetRequiredSubmodules(self):
        return [
            RequiredSubmodule("Common/edk2", True),
            RequiredSubmodule("Common/edk2-platforms", True),
        ]

    def SetArchitectures(self, list_of_requested_architectures):
        unsupported = set(list_of_requested_architectures) - set(
            self.GetArchitecturesSupported())
        if unsupported:
            raise Exception("Unsupported Architecture Requested: " +
                            " ".join(unsupported))
        self.ActualArchitectures = list_of_requested_architectures

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def FilterPackagesToTest(self, changedFilesList, potentialPackagesList):
        for f in changedFilesList:
            if "BaseTools" in f or "platform-build-run-steps.yml" in f:
                return potentialPackagesList.copy()
        return []

    def GetPlatformDscAndConfig(self):
        return ("KarnakPkg/Karnak.dsc", {})

    def GetName(self):
        return "Karnak"

    def GetPackagesPath(self):
        return CommonPlatform.PackagesPath


class PlatformBuilder(UefiBuilder, BuildSettingsManager):
    def __init__(self):
        UefiBuilder.__init__(self)

    def AddCommandLineOptions(self, parserObj):
        parserObj.add_argument(
            "-a", "--arch", dest="build_arch", type=str, default="AARCH64",
            help="AARCH64 only.")

    def RetrieveCommandLineOptions(self, args):
        if args.build_arch.upper() != "AARCH64":
            raise Exception("Only AARCH64 is supported for karnak")

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetPackagesPath(self):
        result = [shell_environment.GetBuildVars().GetValue("FEATURE_CONFIG_PATH", "")]
        result.extend(CommonPlatform.PackagesPath)
        return result

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def GetName(self):
        return "KarnakPkg"

    def GetLoggingLevel(self, loggerType):
        return logging.INFO

    def GetLatestCommit(self):
        try:
            out = subprocess.run(["git", "describe", "--tags", "--always"],
                                 check=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
            return out.stdout.strip()
        except Exception:
            return "Unknown"

    def SetPlatformEnv(self):
        self.env.SetValue("PRODUCT_NAME", "karnak", "Platform Hardcoded")
        self.env.SetValue("ACTIVE_PLATFORM", "KarnakPkg/Karnak.dsc", "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "AARCH64", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANG38", "karnak uses clang")
        self.env.SetValue("EMPTY_DRIVE", "FALSE", "Default to false")
        self.env.SetValue("RUN_TESTS", "FALSE", "Default to false")
        self.env.SetValue("SHUTDOWN_AFTER_RUN", "FALSE", "Default to false")
        self.env.SetValue("BLD_*_BUILDID_STRING", "Unknown", "Default")
        self.env.SetValue("BUILDREPORTING", "TRUE", "Enable build report")
        self.env.SetValue("BUILDREPORT_TYPES",
                          "PCD DEPEX FLASH BUILD_FLAGS LIBRARY FIXED_ADDRESS HASH",
                          "Report types")
        self.env.SetValue("BLD_*_FD_BASE", FD_BASE, "karnak hardware map")
        self.env.SetValue("BLD_*_FD_SIZE", FD_SIZE, "karnak hardware map")
        self.env.SetValue("BLD_*_FIRMWARE_VER", self.GetLatestCommit(), "Latest commit")
        return 0

    def PlatformPreBuild(self):
        return 0

    def PlatformGetOutDir(self):
        target = self.env.GetValue("TARGET")
        toolchain = self.env.GetValue("TOOL_CHAIN_TAG")
        output_dir = self.env.GetValue("OUTPUT_DIRECTORY")
        return os.path.join(output_dir, "{}_{}".format(target, toolchain))

    def PlatformBuildBootShim(self):
        edk2_logging.log_progress("Building Karnak BootShim")
        try:
            subprocess.run(
                ["make", "-C", BOOTSHIM_DIR,
                 "UEFI_BASE=" + FD_BASE,
                 "UEFI_SIZE=" + FD_SIZE,
                 "DTB_STORE=" + DTB_STORE,
                 "DTB_PTR_STORE=" + DTB_PTR_STORE,
                 "DTB_MAX=" + DTB_MAX],
                check=True)
        except subprocess.CalledProcessError:
            logging.critical("Karnak BootShim build failed")
            return 1
        return 0

    def _paths(self):
        platform = self.env.GetValue("PRODUCT_NAME")
        out_dir = self.PlatformGetOutDir()
        return (platform, out_dir,
                os.path.join(out_dir, "FV", platform.upper() + "_UEFI.fd"))

    def PlatformPostBuildBootShim(self):
        edk2_logging.log_progress("Shimming FD")
        platform, out_dir, fd_path = self._paths()
        shimmed = os.path.join(out_dir, "FV", platform.upper() + "_UEFI.fd.shimmed")
        try:
            with open(os.path.join(BOOTSHIM_DIR, "BootShim.bin"), "rb") as shim, \
                    open(fd_path, "rb") as fd, \
                    open(shimmed, "wb") as out:
                out.write(shim.read())
                out.write(fd.read())
        except Exception as exc:
            logging.critical("Failed to create shimmed FD: %s", exc)
            return 1
        return 0

    def PlatformBuildBootImage(self):
        edk2_logging.log_progress("Building boot.img")
        platform, out_dir, _ = self._paths()
        kernel = os.path.join(out_dir, "FV", platform.upper() + "_UEFI.fd.shimmed")
        os.makedirs("out", exist_ok=True)
        output = os.path.join("out", "boot-karnak.img")
        try:
            subprocess.run(
                ["mkbootimg", "--header_version", "4",
                 "--kernel", kernel,
                 "--output", output],
                check=True)
        except Exception as exc:
            logging.critical("mkbootimg failed: %s", exc)
            return 1
        logging.info("boot.img written to %s", output)
        return 0

    def PlatformPostBuild(self):
        for step in (self.PlatformBuildBootShim,
                     self.PlatformPostBuildBootShim,
                     self.PlatformBuildBootImage):
            status = step()
            if status:
                return status
        return 0

    def FlashRomImage(self):
        return 0


if __name__ == "__main__":
    import argparse
    import sys

    from edk2toolext.invocables.edk2_platform_build import Edk2PlatformBuild
    from edk2toolext.invocables.edk2_setup import Edk2PlatformSetup
    from edk2toolext.invocables.edk2_update import Edk2Update

    parser = argparse.ArgumentParser(add_help=False)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--update", "--UPDATE", action="store_true")
    group.add_argument("--setup", "--SETUP", action="store_true")
    args, remaining = parser.parse_known_args()

    sys.argv = ["stuart", "-c", os.path.relpath(__file__)] + remaining
    if args.setup:
        Edk2PlatformSetup().Invoke()
    elif args.update:
        Edk2Update().Invoke()
    else:
        Edk2PlatformBuild().Invoke()
