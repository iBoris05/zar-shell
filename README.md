# ZAR-SHELL — Manual de Usuario

```
███████╗ █████╗ ██████╗     ███████╗██╗  ██╗███████╗██╗     ██╗
╚══███╔╝██╔══██╗██╔══██╗    ██╔════╝██║  ██║██╔════╝██║     ██║
  ███╔╝ ███████║██████╔╝    ███████╗███████║█████╗  ██║     ██║
 ███╔╝  ██╔══██║██╔══██╗    ╚════██║██╔══██║██╔══╝  ██║     ██║
███████╗██║  ██║██║  ██║    ███████║██║  ██║███████╗███████╗███████╗
╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝    ╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝
```

> **Kernel Interface v2.0** — El shell que Windows no merece y tú sí.  
> `CMD.exe` puede irse al carajo. Este es territorio ZAR.

---

## Índice

1. [¿Qué es ZAR-SHELL?](#qué-es-zar-shell)
2. [Compilar e Instalar](#compilar-e-instalar)
3. [El Prompt](#el-prompt)
4. [Navegación y Directorio](#navegación-y-directorio)
5. [Historial Interactivo](#historial-interactivo)
6. [Variables](#variables)
7. [Alias](#alias)
8. [Pipelines y Redirecciones](#pipelines-y-redirecciones)
9. [Procesos en Background](#procesos-en-background)
10. [Todos los Comandos Integrados](#todos-los-comandos-integrados)
11. [Scripts de ZAR-SHELL](#scripts-de-zar-shell)
12. [Atajos de Teclado](#atajos-de-teclado)
13. [Alias por Defecto](#alias-por-defecto)
14. [Privilegios y Seguridad](#privilegios-y-seguridad)
15. [Historial Persistente](#historial-persistente)

---

## ¿Qué es ZAR-SHELL?

ZAR-SHELL es una terminal personalizada para Windows escrita en C++ que **no depende de `cmd.exe`** para nada. Ejecuta binarios directamente, tiene pipelines reales, redirección de I/O, historial persistente con navegación por flechas, alias, variables de sesión, detección de ramas Git, y un prompt premium que hace quedar mal a PowerShell.

Construido con puro Windows API. Sin dependencias externas. Sin licencias de Microsoft.

---

## Compilar e Instalar

### Con MinGW / MSYS2 (recomendado)

```powershell
# Primero asegúrate de tener g++ en tu PATH. Ejemplo con Alire/MSYS2:
$env:Path += ";C:\Users\<TU_USUARIO>\AppData\Local\alire\cache\msys64\ucrt64\bin"

# Compilar (estático, sin DLLs externas)
g++ zar_shell.cpp -o zar_shell.exe -O2 -static -std=c++17

# Ejecutar
.\zar_shell.exe
```

### Con MSVC (Developer Command Prompt de Visual Studio)

```cmd
cl /std:c++17 /O2 zar_shell.cpp /Fe:zar_shell.exe
```

### Poner ZAR-SHELL como terminal por defecto (opcional)

1. Copia `zar_shell.exe` a una carpeta como `C:\Tools\` o `C:\Users\<TU_USUARIO>\bin\`.
2. Agrega esa carpeta a tu variable de entorno `PATH`.
3. En Windows Terminal, ve a **Configuración → Agregar nuevo perfil** y apunta al ejecutable.

---

## El Prompt

El prompt de ZAR-SHELL tiene dos líneas y muestra toda la información importante de un vistazo:

```
╭─ ZAR-NEUTRON  C:/BISMA_LE/Desktop/I+D  [main]  ✓  15:54
╰─ # _
```

| Elemento | Significado |
|---|---|
| `ZAR-NEUTRON` | Identidad del shell (en magenta/cyan) |
| `C:/BISMA_LE/Desktop/I+D` | Directorio actual (abreviado si es muy largo) |
| `[main]` | Rama de Git si estás dentro de un repositorio |
| `✓` | El último comando terminó bien (exit code 0) |
| `✗42` | El último comando falló con código de error 42 |
| `⏱ 3s` | El último comando tardó más de 1 segundo |
| `15:54` | Hora actual |
| `⚡ADMIN` | Aparece si el shell corre con permisos de administrador |

### Rutas largas

Si el directorio actual es muy largo, ZAR-SHELL lo abrevia automáticamente:

```
C:/Users/Fulano/Documents/Proyectos/2026/MiApp/src  →  …/MiApp/src
```

---

## Navegación y Directorio

### `cd` — Cambiar de directorio

```bash
cd C:\Projects          # Ir a una ruta absoluta
cd src                  # Ir a una subcarpeta
cd ..                   # Subir un nivel
cd ../..                # Subir dos niveles
cd -                    # Volver al directorio anterior (¡como en bash!)
cd                      # Sin argumentos: ir a %USERPROFILE% (tu home)
```

### `pwd` — Ver la ruta actual completa

```bash
pwd
# C:/BISMA_LE/Desktop/I+D
```

---

## Historial Interactivo

ZAR-SHELL guarda los últimos **500 comandos** en memoria y en un archivo en disco. Nunca los pierdas.

### Navegar con el teclado

| Tecla | Acción |
|---|---|
| `↑` | Comando anterior en el historial |
| `↓` | Comando siguiente en el historial |
| `←` `→` | Mover cursor dentro del comando actual |
| `Home` | Ir al principio de la línea |
| `End` | Ir al final de la línea |
| `Backspace` | Borrar carácter a la izquierda |
| `Delete` | Borrar carácter a la derecha |
| `Escape` | Borrar toda la línea actual |
| `Ctrl+C` | Cancelar la línea actual |
| `Ctrl+L` | Limpiar pantalla (sin perder lo que estás escribiendo) |

### Ver el historial

```bash
history       # Muestra los últimos 20 comandos numerados
history 50    # Muestra los últimos 50
```

```
 487  dir /b
 488  g++ zar_shell.cpp -o zar_shell.exe -O2 -static
 489  .\zar_shell.exe
```

### Re-ejecutar comandos del historial

```bash
!489    # Vuelve a ejecutar el comando número 489
!1      # Ejecuta el primer comando del historial
```

---

## Variables

ZAR-SHELL maneja dos tipos de variables:

- **Variables de sesión**: solo existen dentro de ZAR-SHELL.
- **Variables de entorno**: se exportan a los procesos hijos.

### `set` — Definir una variable de sesión

```bash
set NOMBRE=ZAR
set BUILD=release
set RUTA=C:/Projects/MiApp
```

### `export` — Definir y exportar al entorno

```bash
export CC=gcc
export CXX=g++
export JAVA_HOME=C:/jdk21
```

### Usar variables en comandos

Las variables se expanden con `$NOMBRE` o `${NOMBRE}`:

```bash
set DIR=C:/Projects
cd $DIR

echo "Compilando en modo $BUILD"

# Variable con nombre compuesto:
echo ${JAVA_HOME}/bin/java
```

### Variables especiales

| Variable | Valor |
|---|---|
| `$?` | Código de salida del último comando |
| `$$` | PID del proceso de ZAR-SHELL |
| `$0` | Nombre del shell (`zarshell`) |

```bash
echo $?    # 0 si el último comando fue exitoso
echo $$    # Ejemplo: 4892
```

### `set` sin argumentos — Ver todas las variables

```bash
set
# NOMBRE=ZAR
# BUILD=release
# RUTA=C:/Projects/MiApp
```

### `unset` — Eliminar una variable

```bash
unset BUILD
unset CC CXX    # Eliminar varias a la vez
```

### `env` — Ver variables de entorno del sistema

```bash
env
# PATH=C:/Windows/System32;...
# USERNAME=...
# (todas las variables de entorno del sistema operativo)
```

---

## Alias

Los alias son atajos que reemplazan un nombre de comando por otro más largo o complejo. Son perfectos para no escribir lo mismo siempre.

### `alias` — Definir un alias

```bash
alias ll=dir /a
alias py=python
alias build=g++ -std=c++17 -O2 -static
alias repo=cd C:/BISMA_LE/Desktop/I+D
```

### Usar un alias

Una vez definido, simplemente lo escribes como si fuera un comando normal:

```bash
ll              # Equivale a: dir /a
build main.cpp -o main.exe
```

### `alias` sin argumentos — Ver todos los alias

```bash
alias
# alias ll=dir /a
# alias py=python
# alias repo=cd C:/BISMA_LE/Desktop/I+D
```

### `unalias` — Eliminar un alias

```bash
unalias ll
unalias py build   # Eliminar varios
```

---

## Pipelines y Redirecciones

ZAR-SHELL implementa pipelines y redirecciones de forma **nativa**, conectando los procesos directamente sin pasar por `cmd.exe`.

### `|` — Pipeline (conectar comandos)

La salida de un comando se convierte en la entrada del siguiente:

```bash
dir /b | findstr ".cpp"
# Lista solo los archivos .cpp del directorio

tasklist | findstr "chrome"
# Buscar si Chrome está corriendo

dir /s /b | find /c ""
# Contar el total de archivos recursivamente
```

### `>` — Redirigir salida a un archivo (sobrescribir)

```bash
dir /b > lista.txt
# Guarda la lista de archivos en lista.txt

ipconfig > red.txt
# Guarda la configuración de red en un archivo
```

### `>>` — Redirigir salida (añadir al final)

```bash
echo Entrada 1 > log.txt
echo Entrada 2 >> log.txt
echo Entrada 3 >> log.txt
# log.txt tendrá las tres líneas
```

### `<` — Leer entrada desde archivo

```bash
findstr "error" < build_output.txt
# Busca "error" dentro del contenido de build_output.txt
```

### Combinaciones

```bash
dir /s /b | findstr ".cpp" > mis_cpp.txt
# Lista todos los .cpp y los guarda en un archivo

type errores.log | findstr "FATAL" >> criticos.log
# Filtra errores fatales y los añade a otro log
```

---

## Procesos en Background

Agrega `&` al final de cualquier comando para que se ejecute en segundo plano sin bloquear la terminal.

```bash
notepad archivo.txt &       # Abre el bloc de notas sin bloquear
mi_servidor.exe &           # Lanza un servidor en background
ping google.com -t > ping_log.txt &   # Ping continuo a archivo
```

El shell te devuelve el PID inmediatamente:

```
[bg] PID 7392
```

### `jobs` — Ver procesos en background

```bash
jobs
# [1] PID 7392  notepad
# [2] PID 8104  mi_servidor
```

### `kill` — Matar un proceso

```bash
kill 7392     # Termina el proceso con ese PID
```

---

## Todos los Comandos Integrados

| Comando | Descripción |
|---|---|
| `cd [dir]` | Cambiar directorio |
| `cd -` | Volver al directorio anterior |
| `pwd` | Mostrar directorio actual |
| `echo [texto]` | Imprimir texto en pantalla |
| `set VAR=val` | Definir variable de sesión |
| `export VAR=val` | Definir variable y exportarla al entorno |
| `unset VAR` | Eliminar variable |
| `env` | Mostrar variables de entorno del sistema |
| `alias name=cmd` | Definir alias |
| `alias` | Listar todos los alias |
| `unalias name` | Eliminar alias |
| `history [n]` | Mostrar historial (últimos N, default 20) |
| `!n` | Re-ejecutar el comando número N |
| `which cmd` | Encontrar la ruta de un ejecutable en PATH |
| `jobs` | Listar procesos en background |
| `kill PID` | Matar proceso por su PID |
| `whoami` | Mostrar usuario y nivel de privilegios |
| `source archivo` | Ejecutar un script de ZAR-SHELL |
| `clear` o `cls` | Limpiar la pantalla |
| `help` | Mostrar ayuda integrada |
| `exit [código]` | Salir del shell (opcionalmente con código de salida) |
| `salir` | Equivalente a `exit` |

---

## Scripts de ZAR-SHELL

Puedes escribir archivos de texto con comandos de ZAR-SHELL y ejecutarlos todos de una vez con `source`.

### Ejemplo: `setup.zsh`

```bash
# setup.zsh — Configuración del entorno de desarrollo
# Las líneas que empiezan con # son comentarios y se ignoran

export CC=gcc
export CXX=g++
export BUILD_DIR=C:/Projects/build

set PROYECTO=MiApp
alias make=mingw32-make

echo "Entorno listo para $PROYECTO"
cd C:/Projects/MiApp
```

### Ejecutar el script

```bash
source setup.zsh
```

### Separar comandos en una sola línea con `;`

```bash
cd C:/Projects ; g++ main.cpp -o app.exe ; .\app.exe
```

---

## Atajos de Teclado

| Atajo | Acción |
|---|---|
| `↑` / `↓` | Navegar por el historial |
| `←` / `→` | Mover cursor dentro de la línea |
| `Home` | Inicio de línea |
| `End` | Final de línea |
| `Backspace` | Borrar caracter anterior |
| `Delete` | Borrar caracter siguiente |
| `Escape` | Limpiar la línea completa |
| `Ctrl+C` | Cancelar entrada actual |
| `Ctrl+L` | Limpiar pantalla |

---

## Alias por Defecto

ZAR-SHELL viene con alias preconfigurados para que te sientas como en casa si vienes de Linux:

| Alias | Equivale a |
|---|---|
| `ll` | `dir /a` |
| `ls` | `dir /b` |
| `la` | `dir /a /b` |
| `grep` | `findstr` |
| `cat` | `type` |
| `rm` | `del` |
| `rmdir` | `rd /s /q` |
| `cp` | `copy` |
| `mv` | `move` |
| `ps` | `tasklist` |
| `top` | `tasklist /v` |
| `ifconfig` | `ipconfig /all` |
| `..` | `cd ..` |
| `...` | `cd ../..` |
| `....` | `cd ../../..` |

---

## Privilegios y Seguridad

Al iniciarse, ZAR-SHELL intenta obtener los siguientes privilegios del sistema operativo (solo funciona si lo corres como Administrador):

| Privilegio | Para qué sirve |
|---|---|
| `SeDebugPrivilege` | Inspeccionar y manipular cualquier proceso |
| `SeBackupPrivilege` | Leer cualquier archivo ignorando permisos |
| `SeRestorePrivilege` | Escribir en cualquier archivo ignorando permisos |
| `SeTakeOwnershipPrivilege` | Tomar posesión de cualquier objeto del sistema |

Si el shell detecta que está corriendo como Administrador, muestra el indicador `⚡ADMIN` en el prompt.

Para correr como administrador: click derecho sobre `zar_shell.exe` → **Ejecutar como administrador**.

El comando `whoami` te dice el nivel de privilegios actual:

```bash
whoami
# ZAR-NEUTRON@TU-PC
# [⚡] Corriendo como ADMINISTRADOR — con gran poder...
```

---

## Historial Persistente

El historial de comandos se guarda automáticamente cuando sales del shell y se carga cuando lo abres de nuevo. El archivo se almacena en:

```
C:\Users\<TU_USUARIO>\AppData\Roaming\ZarShell\history.txt
```

- Guarda hasta **500 comandos**.
- Los comandos duplicados consecutivos no se guardan dos veces.
- Puedes editar o borrar el archivo de historial manualmente si quieres.

---

## Ejemplos Prácticos

### Buscar archivos por extensión

```bash
dir /s /b | findstr "\.cpp$"
```

### Compilar un proyecto C++

```bash
cd C:/MiProyecto
g++ -std=c++17 -O2 src/*.cpp -o bin/app.exe
```

### Ver qué puertos están escuchando

```bash
netstat -an | findstr "LISTEN"
```

### Ver los procesos que más memoria usan

```bash
tasklist /v | sort
```

### Crear un archivo de log de sesión

```bash
dir /s > session_log.txt
date /t >> session_log.txt
```

### Lanzar un servidor y seguir trabajando

```bash
mi_servidor.exe &
# [bg] PID 9341
jobs
echo "Servidor corriendo, puedo seguir trabajando"
```

---

*ZAR-SHELL — Porque un buen shell no debería venir de Redmond.*
