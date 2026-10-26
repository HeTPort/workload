#!/usr/bin/env python3
"""CPU/GPU/NPU/relay/Monitor 集中构建入口，仅依赖 Python 标准库。"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
if hasattr(sys.stdout, 'reconfigure'): sys.stdout.reconfigure(encoding='utf-8')
if hasattr(sys.stderr, 'reconfigure'): sys.stderr.reconfigure(encoding='utf-8')
from datetime import datetime, timezone
import uuid

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ('cpu', 'gpu', 'npu', 'relay', 'monitor')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''): digest.update(block)
    return digest.hexdigest()


def revision(path):
    try:
        result = subprocess.run(['git','-c',f'safe.directory={path.as_posix()}','-C',str(path),'rev-parse','HEAD'],capture_output=True,text=True,timeout=10)
        dirty = subprocess.run(['git','-c',f'safe.directory={path.as_posix()}','-C',str(path),'status','--porcelain'],capture_output=True,text=True,timeout=10)
        return {'commit':result.stdout.strip() if result.returncode==0 else None,'dirty':bool(dirty.stdout.strip()),'path':str(path)}
    except (OSError, subprocess.TimeoutExpired): return {'commit':None,'dirty':None,'path':str(path)}


def atomic_json(path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    os.replace(temporary,path)


def selected(value):
    result=list(dict.fromkeys(value.split(',')))
    if result==['all']: result=['cpu','gpu','npu','relay']
    if not result or set(result)-set(COMPONENTS): raise ValueError('components 应为 cpu,gpu,npu,relay,monitor 或 all')
    return result


def ndk_paths(args):
    if args.target=='host': return None
    raw=args.ndk_root or os.environ.get('ANDROID_NDK_HOME' if args.target=='android-arm64' else 'OHOS_NDK_HOME')
    if not raw: raise ValueError('交叉编译必须提供 --ndk-root 或 ANDROID_NDK_HOME/OHOS_NDK_HOME，不会回退主机编译')
    root=Path(raw).expanduser().resolve(strict=True)
    toolchain=root/('build/cmake/android.toolchain.cmake' if args.target=='android-arm64' else 'build/cmake/ohos.toolchain.cmake')
    if not toolchain.is_file(): raise ValueError(f'NDK toolchain 不存在：{toolchain}')
    return root,toolchain


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--components',default='cpu,gpu,npu',help='逗号分隔；all=cpu,gpu,npu,relay')
    p.add_argument('--target',choices=('host','android-arm64','ohos-arm64'),default='host')
    p.add_argument('--configuration',choices=('Release','Debug'),default='Release')
    p.add_argument('--engine',choices=('auto','cmake','direct'),default='auto')
    p.add_argument('--gpu-apis',default='null',help='null,gles,vulkan,opencl；默认只构建主机框架后端')
    p.add_argument('--monitor-root',type=Path,default=ROOT.parent/'npu-monitor')
    p.add_argument('--output-root',type=Path,default=ROOT/'build/unified')
    p.add_argument('--ndk-root',type=Path)
    p.add_argument('--android-api',type=int,default=24)
    p.add_argument('--platform',default='kirin9030',help='relay 交接的平台标记，不修改设备')
    p.add_argument('--jobs',type=int,default=min(os.cpu_count() or 2,8))
    p.add_argument('--compiler',help='direct 模式 C++ 编译器')
    p.add_argument('--c-compiler',help='relay 的 C 编译器')
    p.add_argument('--cmake-arg',action='append',default=[],help='额外 CMake 参数，写作 --cmake-arg=-D...')
    p.add_argument('--generator',help='例如 Ninja 或 MinGW Makefiles')
    p.add_argument('--test',action='store_true',help='仅主机：运行实际编译产物的短自检；不代表板端验收')
    p.add_argument('--dry-run',action='store_true',help='输出构建计划，不运行编译器')
    p.add_argument('--include-npu',action='store_true',help='Monitor 打包前的 package 校验包含 NPU 资产')
    p.add_argument('--doctor',action='store_true',help='检查所选构建路径的前置环境')
    return p


class Build:
    def __init__(self,args):
        self.args=args
        self.components=selected(args.components)
        self.apis=list(dict.fromkeys(args.gpu_apis.split(',')))
        if set(self.apis)-{'null','gles','vulkan','opencl'}: raise ValueError('不支持的 GPU API')
        if args.jobs<1: raise ValueError('jobs 必须为正整数')
        if args.test and args.target!='host': raise ValueError('交叉编译产物不能在主机执行；请移除 --test')
        if 'monitor' in self.components and args.target!='host': raise ValueError('Monitor 在主机打包，不能选择 NDK target')
        if 'relay' in self.components and args.target=='host' and os.name=='nt': raise ValueError('Windows 原生没有 termios；relay 主机自检请用 WSL/Linux，或选择 NDK target')
        self.ndk=ndk_paths(args)
        self.cmake=shutil.which('cmake')
        self.engine=('cmake' if self.cmake else 'direct') if args.engine=='auto' else args.engine
        if self.ndk and self.engine=='direct' and set(self.components)-{'relay'}: raise ValueError('workload 交叉编译必须使用 CMake；请配置 CMake/Ninja')
        if self.engine=='cmake' and not self.cmake and set(self.components)-{'relay','monitor'}: raise ValueError('找不到 cmake，请安装或用 host/direct')
        self.cxx=args.compiler or shutil.which('g++') or shutil.which('clang++')
        self.cc=args.c_compiler or shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if self.engine=='direct' and set(self.components)&{'cpu','gpu','npu'} and not self.cxx: raise ValueError('找不到主机 C++ 编译器 g++/clang++')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:6]
        self.out=args.output_root.resolve()/args.target/args.configuration.lower()/stamp
        self.manifest={'schema_version':1,'status':'planned','target':args.target,'host':platform.platform(),
            'configuration':args.configuration,'engine':self.engine,'gpu_apis':self.apis,'dry_run':args.dry_run,
            'sources':{'workload':revision(ROOT),'monitor':revision(args.monitor_root.resolve())},
            'tools':{'python':sys.version,'cxx':str(self.cxx),'cc':str(self.cc),'cmake':self.cmake},
            'ndk_root':str(self.ndk[0]) if self.ndk else None,'components':{},'commands':[],'started_at':datetime.now(timezone.utc).isoformat()}

    def command(self,component,argv,cwd=ROOT):
        argv=list(map(str,argv))
        print(f'[{component}] '+(subprocess.list2cmdline(argv) if os.name=='nt' else shlex.join(argv)),flush=True)
        item={'component':component,'argv':argv,'cwd':str(cwd),'returncode':None}
        self.manifest['commands'].append(item)
        if self.args.dry_run: return
        log=self.out/f'{component}.log'
        with log.open('a',encoding='utf-8') as stream:
            stream.write('\n$ '+json.dumps(argv,ensure_ascii=False)+'\n');stream.flush()
            result=subprocess.run(argv,cwd=cwd,stdout=stream,stderr=subprocess.STDOUT)
        item['returncode']=result.returncode
        if result.returncode: raise RuntimeError(f'{component} 失败，退出码 {result.returncode}，日志 {log}')

    def direct(self,kind,directory):
        project=ROOT/f'{kind}workload'
        sources=sorted((project/'src').glob('*.cpp'))
        flags=['-std=c++17','-O2' if self.args.configuration=='Release' else '-O0','-g' if self.args.configuration=='Debug' else '-DNDEBUG','-Wall','-Wextra','-Wpedantic','-pthread','-I',str(project/'include'),'-I',str(project/'src')]
        libraries=[]
        if kind=='gpu':
            for api in ('null','gles','vulkan','opencl'):
                enabled=api in self.apis
                flags.append(f'-DGPU_AVS_ENABLE_{api.upper()}={int(enabled)}')
                if enabled: sources+=sorted((project/'src/backends'/api).glob('*.cpp'))
            flags.append(f'-DGPU_AVS_ENABLE_VULKAN_GRAPHICS={int("vulkan" in self.apis)}')
            if 'gles' in self.apis: libraries+=['-lEGL','-lGLESv2']
            if 'vulkan' in self.apis: libraries+=['-lvulkan']
            if 'opencl' in self.apis: libraries+=['-lOpenCL']
        else: sources+=sorted((project/'src/backends').rglob('*.cpp'))
        if kind=='gpu' and 'vulkan' in self.apis:
            validator=shutil.which('glslangValidator')
            if not validator: raise ValueError('Vulkan direct 构建需要主机 glslangValidator 编译 SPIR-V')
            shader_dir=directory/'shaders/vulkan';shader_dir.mkdir(parents=True,exist_ok=True)
            for shader in ('workload.comp','fullscreen.vert','workload.frag'):
                self.command(kind,[validator,'-V',project/'shaders/vulkan'/shader,'-o',shader_dir/(shader+'.spv')])
        if kind=='npu' and sys.platform.startswith('linux'): libraries+=['-ldl']
        if os.name=='nt': flags+=['-static-libgcc','-static-libstdc++']
        binary=directory/(f'{kind}-avs-workload'+('.exe' if os.name=='nt' else ''))
        self.command(kind,[self.cxx,*flags,*sources,*libraries,'-o',binary])
        if kind=='npu' and self.args.test:
            contract_sources=['tests/contract_tests.cpp','src/json.cpp','src/manifest.cpp','src/profile.cpp','src/sha256.cpp','src/tensor.cpp','src/backends/null/null_backend.cpp']
            contract=directory/('npu-contract-tests'+('.exe' if os.name=='nt' else ''))
            self.command(kind,[self.cxx,*flags,*[project/f for f in contract_sources],'-o',contract])
            fake=directory/('npu-fake-tflite-runtime.dll' if os.name=='nt' else 'libnpu-fake-tflite-runtime.so')
            self.command(kind,[self.cxx,*flags,'-shared',*(['-fPIC'] if os.name!='nt' else []),project/'tests/fake_tflite_runtime.cpp','-o',fake])
        return binary

    def cmake_workload(self,kind,directory):
        config=[self.cmake,'-S',ROOT/f'{kind}workload','-B',directory,f'-DCMAKE_BUILD_TYPE={self.args.configuration}',f'-DBUILD_TESTING={"ON" if self.args.test else "OFF"}']
        if self.args.generator: config+=['-G',self.args.generator]
        if self.ndk:
            config+=[f'-DCMAKE_TOOLCHAIN_FILE={self.ndk[1]}']
            config+=([f'-DANDROID_ABI=arm64-v8a',f'-DANDROID_PLATFORM=android-{self.args.android_api}','-DANDROID_STL=c++_static'] if self.args.target=='android-arm64' else ['-DOHOS_ARCH=arm64-v8a','-DOHOS_STL=c++_static'])
        if kind=='gpu':
            config += [f'-DGPU_AVS_ENABLE_{api.upper()}={"ON" if api in self.apis else "OFF"}' for api in ('null','gles','vulkan','opencl')]
        config+=self.args.cmake_arg
        self.command(kind,config)
        self.command(kind,[self.cmake,'--build',directory,'--config',self.args.configuration,'--parallel',self.args.jobs])
        suffix='.exe' if os.name=='nt' and not self.ndk else ''
        binary=directory/(f'{kind}-avs-workload'+suffix)
        if not self.args.dry_run and not binary.exists():
            candidates=list(directory.rglob(binary.name))
            if len(candidates)!=1: raise RuntimeError(f'无法唯一定位 {kind} 产物')
            binary=candidates[0]
        return binary

    def relay(self,directory):
        source=self.args.monitor_root.resolve()/'native/uart_relay/avs_uart_relay.c'
        if not source.is_file(): raise ValueError(f'relay 源码不存在：{source}')
        compiler=self.cc;flags=['-std=c11','-O2','-Wall','-Wextra','-Werror']
        if self.ndk:
            llvm=self.ndk[0]/('toolchains/llvm/prebuilt' if self.args.target=='android-arm64' else 'native/llvm')
            if self.args.target=='android-arm64':
                host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
                llvm=llvm/host
            compiler=llvm/'bin'/('clang.exe' if os.name=='nt' else 'clang')
            sysroot=llvm/'sysroot' if self.args.target=='android-arm64' else self.ndk[0]/'native/sysroot'
            flags += [f'--target=aarch64-linux-android{self.args.android_api}' if self.args.target=='android-arm64' else '--target=aarch64-linux-ohos',f'--sysroot={sysroot}']
            if not compiler.is_file() or not sysroot.is_dir(): raise ValueError('relay NDK 编译器/sysroot 缺失；请核对 SDK 的 native 或 LLVM 布局')
        if not compiler: raise ValueError('找不到 C 编译器')
        output=directory/'avs-uart-relay'
        self.command('relay',[compiler,*flags,source,'-o',output])
        if self.args.test: self.command('relay',[output,'--self-test'])
        return output

    def test(self,kind,binary):
        if not self.args.test: return
        self.command(kind,[binary,'--capabilities'])
        if kind=='cpu': args=['--profile','integer','--duration','0','--batches','3','--warmup','0','--iterations','1000','--summary-only']
        elif kind=='gpu':
            if 'null' not in self.apis: raise ValueError('--test 的 GPU 主机框架测试需要 --gpu-apis 包含 null')
            args=['--profile','mixed','--api','null','--duration','0','--frames','3','--warmup','0','--width','8','--height','8','--summary-only']
        else:
            self.command(kind,[sys.executable,ROOT/'npuworkload/tests/test_monitor_contract.py','--executable',binary,'-v'])
            contract=binary.parent/('npu-contract-tests'+('.exe' if os.name=='nt' else ''))
            self.command(kind,[contract,*[ROOT/f'npuworkload/profiles/{name}/manifest.json' for name in ('framework_smoke','kws01','ic01','ad01','sww01')]])
            return
        self.command(kind,[binary,*args])

    def run(self):
        print(json.dumps({'target':self.args.target,'engine':self.engine,'components':self.components,'output':str(self.out),'ndk_ready':bool(self.ndk)},ensure_ascii=False,indent=2))
        if self.args.doctor: return 0
        self.out.mkdir(parents=True,exist_ok=False)
        self.manifest['status']='running'
        try:
            if not self.args.dry_run:
                if self.engine=='direct' and set(self.components)&{'cpu','gpu','npu'}: self.command('toolchain',[self.cxx,'--version'])
                if self.engine=='cmake' and self.cmake: self.command('toolchain',[self.cmake,'--version'])
            for component in self.components:
                directory=self.out/component;directory.mkdir()
                self.manifest['components'][component]={'status':'running'}
                if component=='monitor':
                    monitor=self.args.monitor_root.resolve()
                    self.command(component,[sys.executable,'main.py','validate','--package',*(['--include-npu'] if self.args.include_npu else [])],cwd=monitor)
                    self.command(component,[sys.executable,'-m','PyInstaller','--clean','--noconfirm','vmin_judge.spec'],cwd=monitor)
                    binary=monitor/'dist'/('vmin_judge.exe' if os.name=='nt' else 'vmin_judge')
                elif component=='relay': binary=self.relay(directory)
                else:
                    binary=self.direct(component,directory) if self.engine=='direct' else self.cmake_workload(component,directory)
                    self.test(component,binary)
                record=self.manifest['components'][component]
                record.update(status='planned' if self.args.dry_run else 'success',binary=str(binary))
                if not self.args.dry_run:
                    if not binary.is_file(): raise RuntimeError(f'产物不存在：{binary}')
                    record.update(sha256=sha256(binary),size=binary.stat().st_size)
            self.manifest['status']='planned' if self.args.dry_run else 'success'
            return 0
        except (OSError,ValueError,RuntimeError) as exc:
            self.manifest.update(status='failed',error=str(exc))
            if 'component' in locals(): self.manifest['components'][component]['status']='failed'
            print(str(exc),file=sys.stderr)
            return 1
        finally:
            self.manifest['finished_at']=datetime.now(timezone.utc).isoformat()
            atomic_json(self.out/'build-manifest.json',self.manifest)
            print(f'构建记录：{self.out / "build-manifest.json"}')


def main(argv=None):
    args=parser().parse_args(argv)
    try: return Build(args).run()
    except (ValueError,OSError) as exc:
        print(f'环境或参数错误：{exc}',file=sys.stderr);return 2


if __name__=='__main__': raise SystemExit(main())
