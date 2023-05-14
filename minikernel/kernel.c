/*
 *  kernel/kernel.c
 *
 *  Minikernel. Versin 1.0
 *
 *  Fernando Prez Costoya
 *
 */

/*
 *
 * Fichero que contiene la funcionalidad del sistema operativo
 *
 */

#include "kernel.h"	/* Contiene defs. usadas por este modulo */
#include <string.h> /* Need string management*/
/*
 *
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
 *
 */

/*
 * Funcin que inicia la tabla de procesos
 */
static void iniciar_tabla_proc(){
	int i;

	for (i=0; i<MAX_PROC; i++){
		tabla_procs[i].estado = NO_USADA;
		for(int j=0; j<NUM_MUT_PROC; j++){
			tabla_procs[i].descriptores_mutex[j] = NO_USADO;
		}
	}
}

/*
 * Funcin que busca una entrada libre en la tabla de procesos
 */
static int buscar_BCP_libre(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		if (tabla_procs[i].estado == NO_USADA)
			return i;
	return ERROR_GENERICO;
}

/*
*	Funciones relacionadas con la tabla de mutex:
*	iniciar_tabla_mutex buscar_mutex_libre buscar_mutex_repetidos
* NO_USADO
*/
static void iniciar_tabla_mutex(){
	for (int i=0; i<NUM_MUT; i++){
		tabla_mutex[i].estado = NO_USADO;
		cpy(tabla_mutex[i].nombre, "");
		tabla_mutex[i].n_proc_asociados = 0;
		tabla_mutex[i].id_proceso_lock = NO_USADO;
		tabla_mutex[i].n_veces_lock = 0;
		tabla_mutex[i].procesos_bloqueados.primero = NULL;
	}

}

static int buscar_mutex_libre_y_no_repetido(char *nombre){
	for (int i=0; i<NUM_MUT; i++){
		if (tabla_mutex[i].estado != NO_USADO && cmp(tabla_mutex[i].nombre, nombre) == 1){
			return ERROR_NOMBRE_REPETIDO;
		}
		if (tabla_mutex[i].estado==NO_USADO)
			return i;
	}
	return ERROR_MAX_NUM_MUTEX;
}

static int buscar_mutex(char *nombre){
	for (int i=0; i<NUM_MUT; i++){
		if (cmp(tabla_mutex[i].nombre, nombre) == 1 ){
			return i;
		}
	}
	return ERROR_MUTEX_NO_EXISTE;
}

/*
*	Funciones auxiliares del mutex para los procesos:
*	buscar_descriptor_mutex_proc obtener_mutex_proc
*/
static int buscar_descriptor_mutex_proc(unsigned int mutexid){
	for(int i = 0; i < NUM_MUT_PROC; i++){
		if(p_proc_actual->descriptores_mutex[i] == mutexid){
			return i;
		}
	}
	return ERROR_GENERICO;
}

static int obtener_mutex_proc(){
	for(int i = 0; i < NUM_MUT_PROC; i++){
		if(p_proc_actual->descriptores_mutex[i] == NO_USADO){
			return i;
		}
	}
	return ERROR_GENERICO;
}

/*
 *
 * Funciones que facilitan el manejo de las listas de BCPs
 *	insertar_ultimo eliminar_primero eliminar_elem
 *
 * NOTA: PRIMERO SE DEBE LLAMAR A eliminar Y LUEGO A insertar
 */

/*
 * Inserta un BCP al final de la lista.
 */
static void insertar_ultimo(lista_BCPs *lista, BCP * proc){
	if (lista->primero==NULL)
		lista->primero= proc;
	else
		lista->ultimo->siguiente=proc;
	lista->ultimo= proc;
	proc->siguiente=NULL;
}

/*
 * Elimina el primer BCP de la lista.
 */
static void eliminar_primero(lista_BCPs *lista){

	if (lista->ultimo==lista->primero)
		lista->ultimo=NULL;
	lista->primero=lista->primero->siguiente;
}

/*
 * Elimina un determinado BCP de la lista.
*/
static void eliminar_elem(lista_BCPs *lista, BCP * proc){
	BCP *paux=lista->primero;

	if (paux==proc)
		eliminar_primero(lista);
	else {
		for ( ; ((paux) && (paux->siguiente!=proc));
			paux=paux->siguiente);
		if (paux) {
			if (lista->ultimo==paux->siguiente)
				lista->ultimo=paux;
			paux->siguiente=paux->siguiente->siguiente;
		}
	}
}

/*
 *
 * Funciones relacionadas con la planificacion
 *	espera_int planificador
 */

/*
 * Espera a que se produzca una interrupcion
 */
static void espera_int(){
	int nivel;

	//printk("-> NO HAY LISTOS. ESPERA INT\n");

	/* Baja al mnimo el nivel de interrupcin mientras espera */
	nivel=fijar_nivel_int(NIVEL_1);
	halt();
	fijar_nivel_int(nivel);
}

/*
 * Funcin de planificacion que implementa un algoritmo FIFO.
 */
static BCP * planificador(){
	while (lista_listos.primero==NULL)
		espera_int();		/* No hay nada que hacer */
	return lista_listos.primero;
}

/*
 *
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso(){
	printk("-> LIBERANDO PROCESO %d\n", p_proc_actual->id);
	// Liberamos mutex abiertos
	for(int i = 0; i < NUM_MUT_PROC; i++){
		int mutexid = p_proc_actual->descriptores_mutex[i];
		if(mutexid == NO_USADO) 
			continue;
		
		escribir_registro(1, mutexid);
		cerrar_mutex();
	}
	
	BCP * p_proc_anterior;
	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */

	p_proc_actual->estado=TERMINADO;
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
	eliminar_primero(&lista_listos); /* proc. fuera de listos */
	fijar_nivel_int(nivel_interrupcion_previo);

	/* Realizar cambio de contexto */
	p_proc_anterior=p_proc_actual;
	p_proc_actual=planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
			p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
    return; /* no debera llegar aqui */
}

/*
 *
 * Funciones relacionadas con el tratamiento de interrupciones
 *	excepciones: exc_arit exc_mem
 *	interrupciones de reloj: int_reloj
 *	interrupciones del terminal: int_terminal
 *	llamadas al sistemas: llam_sis
 *	interrupciones SW: int_sw
 *
 */

/*
 * Tratamiento de excepciones aritmeticas
 */
static void exc_arit(){

	if (!viene_de_modo_usuario())
		panico("excepcion aritmetica cuando estaba dentro del kernel");


	printk("-> EXCEPCION ARITMETICA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no debera llegar aqui */
}

/*
 * Tratamiento de excepciones en el acceso a memoria
 * Si se accede a un parametro  de la zona de usuario, no se debe producir panico, sino que se aborta el proceso
 */
static void exc_mem(){

	if (!viene_de_modo_usuario() && zona_mem_proc_usuario == 0)
		panico("excepcion de memoria cuando estaba dentro del kernel");


	printk("-> EXCEPCION DE MEMORIA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no debera llegar aqui */
}

/*
 * Tratamiento de interrupciones de terminal
 */
static void int_terminal(){
	char car;

	car = leer_puerto(DIR_TERMINAL);
	printk("-> TRATANDO INT. DE TERMINAL %c\n", car);

        return;
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj(){
	num_int_reloj++;
	//printk("-> TRATANDO INT. DE RELOJ Nº %d\n", num_int_reloj);
	tratamiento_uso_procesador();
	tratamiento_int_dormir();
    return;
}

/*
 * Tratamiento de llamadas al sistema
 */
static void tratar_llamsis(){
	int nserv, res;

	nserv=leer_registro(0);
	if (nserv<NSERVICIOS)
		res=(tabla_servicios[nserv].fservicio)();
	else
		res=-1;		/* servicio no existente */
	escribir_registro(0,res);
	return;
}

/*
 * Tratamiento de interrupciuones software
 */
static void int_sw(){

	printk("-> TRATANDO INT. SW\n");

	return;
}

/*
 *
 * Funcion auxiliar que crea un proceso reservando sus recursos.
 * Usada por llamada crear_proceso.
 *
 */
static int crear_tarea(char *prog){
	void * imagen, *pc_inicial;
	int error=0;
	int proc;
	BCP *p_proc;

	proc=buscar_BCP_libre();
	if (proc==-1)
		return -1;	/* no hay entrada libre */

	/* A rellenar el BCP ... */
	p_proc=&(tabla_procs[proc]);

	/* crea la imagen de memoria leyendo ejecutable */
	imagen=crear_imagen(prog, &pc_inicial);
	if (imagen)	{	
		p_proc->info_mem=imagen;
		p_proc->pila=crear_pila(TAM_PILA);
		fijar_contexto_ini(p_proc->info_mem, p_proc->pila, TAM_PILA, pc_inicial, &(p_proc->contexto_regs));
		p_proc->id=proc;
		p_proc->estado=LISTO;
		
		p_proc->dormir = 0;
		p_proc->tiempo_usuario = 0;
		p_proc->tiempo_sistema = 0;

		for(int i = 0; i < NUM_MUT_PROC; i++){
			p_proc->descriptores_mutex[i] = NO_USADO;
		}
		p_proc->num_mutex = 0;

		/* lo inserta al final de cola de listos */
		int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
		insertar_ultimo(&lista_listos, p_proc);
		error= 0;
		fijar_nivel_int(nivel_interrupcion_previo);
	}
	else
		error= -1; /* fallo al crear imagen */

	return error;
}

/*
 *
 * Rutinas que llevan a cabo las llamadas al sistema
 *	sis_crear_proceso sis_escribir
 *
 */

/*
 * Tratamiento de llamada al sistema crear_proceso. Llama a la
 * funcion auxiliar crear_tarea sis_terminar_proceso
 */
int sis_crear_proceso(){
	char *prog;
	int res;

	printk("-> PROC %d: CREAR PROCESO\n", p_proc_actual->id);
	prog=(char *)leer_registro(1);
	res=crear_tarea(prog);
	return res;
}

/*
 * Tratamiento de llamada al sistema escribir. Llama simplemente a la
 * funcion de apoyo escribir_ker
 */
int sis_escribir()
{
	char *texto;
	unsigned int longi;

	texto=(char *)leer_registro(1);
	longi=(unsigned int)leer_registro(2);

	escribir_ker(texto, longi);
	return 0;
}

/*
 * Tratamiento de llamada al sistema terminar_proceso. Llama a la
 * funcion auxiliar liberar_proceso
 */
int sis_terminar_proceso(){
	printk("-> FIN PROCESO %d\n", p_proc_actual->id);
	liberar_proceso();
    return 0; /* no debera llegar aqui */
}
//-----------------------------------------------------------------------------------------

/**
* Funcion que retorna el id del proceso que la invoca
*/
int obtener_id_pr(){
	return p_proc_actual->id;
}

/**
* Llamada que permita que un proceso pueda qudarse bloqueado un plazo de tiempo
* Pautas:
* Modificar el BCP para incluir algún campo relacionado con esta llamada.
* Definir una lista de procesos esperando plazos.
* Incluir la llamada que, entre otras labores, debe poner al proceso en estado bloqueado, reajustar las listas de
BCPs correspondientes y realizar el cambio de contexto.
* Añadir a la rutina de interrupción la detección de si se cumple el plazo de algún proceso dormido. Si es así,
debe cambiarle de estado y reajustar las listas correspondientes.
* Revisar el código del sistema para detectar posibles problemas de sincronización y solucionarlos
adecuadamente.
* Se usan los registros para el paso de parametros del sistema
*/
int dormir(){
	unsigned int segundos = (unsigned int) leer_registro(1);
	//printk("-> PROC %d: DORMIR %d SEGUNDOS\n", p_proc_actual->id, segundos);
	// Fijar nivel de interrupción a 3
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);

	BCP *p_proc = p_proc_actual;
	p_proc->estado = BLOQUEADO;
	// Los procesos duerment el numero de ticks apropiados
	p_proc->dormir = segundos*TICK;

	// Reajustar listas BCP (primero se elimina y luego se inserta)
	eliminar_primero(&lista_listos);
	insertar_ultimo(&lista_bloq_dormir, p_proc);


	// Cambio contexto voluntario
	p_proc_actual = planificador();
	cambio_contexto(&(p_proc->contexto_regs), &(p_proc_actual->contexto_regs));
	
	// Restaurar nivel de interrupción
	fijar_nivel_int(nivel_interrupcion_previo);

	return 0;
}

void tratamiento_int_dormir(){
	//printk("-> TRATANDO INT. DE RELOJ PARA PROCESOS DORMIDOS\n");

	BCP *p_proc = lista_bloq_dormir.primero;
	BCP *siguiente = NULL;

	while(p_proc != NULL){
		siguiente = p_proc->siguiente;
		p_proc->dormir--;
		if(p_proc->dormir == 0){
			int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);

			p_proc->estado = LISTO;
			eliminar_elem(&lista_bloq_dormir, p_proc);
			insertar_ultimo(&lista_listos, p_proc);
			
			fijar_nivel_int(nivel_interrupcion_previo);
		
		}
		p_proc = siguiente;
	}
}

/**
* Llamada que devuelve el numero de interrupciones que se han producio desde que arrancó el sistema. 
* Si recibe un puntero no nulo, devuelve cuantas veces en la interrupcion de reloj se ha detectado que el proceso estaba ejecutado en modo usuario (campo usuario) y cuantas en modo sistema (campo sistema).
* Si no hay proceso listo, estará ejecutando el último que se bloqueó. En este caso, no hay que imputarle tiempo de ejecución.
*/
int tiempos_proceso(){
	//printk("-> PROC %d: TIEMPOS PROCESO\n", p_proc_actual->id);
	struct tiempos_ejec *t_ejec = (struct tiempos_ejec *) leer_registro(1);
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
	if(t_ejec != NULL){
		// Flag para saber que estamos en zona de memoria de proceso usuario
		zona_mem_proc_usuario = 1;
		t_ejec->usuario = p_proc_actual->tiempo_usuario;
		t_ejec->sistema = p_proc_actual->tiempo_sistema;
		zona_mem_proc_usuario = 0;
	}
	fijar_nivel_int(nivel_interrupcion_previo);
	return num_int_reloj;
}
void tratamiento_uso_procesador(){
	//printk("-> SUMANDO TICKS AL PROCESO\n");
	BCPptr p_proc = lista_listos.primero;
	if(p_proc != NULL){
		if(viene_de_modo_usuario()){
			p_proc_actual->tiempo_usuario++;
		}else{
			p_proc_actual->tiempo_sistema++;
		}
	}
}

/**
* Crear mutex con el nombre y tipo especificados.
* Devuelve un entero que representa un descriptor para acceder al mutex. En caso de error devuelve un numero negativo.
*/
int crear_mutex(){
	char *nombre = strdup((char *) leer_registro(1));
	int tipo = (int) leer_registro(2);
	printk("-> PROC %d: CREAR MUTEX %s\n", p_proc_actual->id, nombre);

	// Comprobar que el nombre no sea demasiado largo
	if(len(nombre) > MAX_NOM_MUT){
		printk("--->ERROR: El nombre del mutex es demasiado largo\n");
		return ERROR_LONGITUD_NOMBRE;
	}

	// Comprobar que proceso no tiene mas de NUM_MUT_PROC
	if(p_proc_actual->num_mutex >= NUM_MUT_PROC){
		printk("--->ERROR: El proceso %d tiene demasiados mutex\n", p_proc_actual->id);
		return ERROR_MAX_NUM_MUTEX_PROC;
	}
	printk("--> PROC %d: INTENTA CREAR MUTEX %s\n", p_proc_actual->id, nombre);
	// Comprobar que no se ha alcanzado el maximo numero de mutex
	while(num_mutex_global >= NUM_MUT){
		printk("--->ERROR: Creando %s. Se ha alcanzado el maximo numero de mutex, %d \n", nombre, num_mutex_global);
		esperar_hueco_mutex(nombre);
	}
	// Volvemos a cogerlo por si nos bloqueamos para que se actualice el descriptor
	int descriptor_mutex = buscar_mutex_libre_y_no_repetido(nombre);
	int error;
	if ((error = cumple_requisitos(nombre, descriptor_mutex)) < 0){
		return error;
	}


	// Crear mutex
	Mutex *mutex;
	mutex = &tabla_mutex[descriptor_mutex];
	mutex->tipo = tipo;
	mutex->estado = LIBRE;
	mutex->procs_asociados[mutex->n_proc_asociados] = p_proc_actual->id;
	cpy(mutex->nombre, nombre);

	// Asociar mutex al proceso
	int pos_mutex_proc = obtener_mutex_proc();
	p_proc_actual->descriptores_mutex[pos_mutex_proc] = descriptor_mutex;

	// Actualizar estados
	mutex->n_proc_asociados++;
	printk("----> ACTUALIZACION: MUTEX %s TIENE %d PROCESOS ASOCIADOS\n", mutex->nombre, mutex->n_proc_asociados);
	p_proc_actual->num_mutex++;
	printk("----> ACTUALIZACION: PROC %d TIENE %d MUTEX ASOCIADOS\n", p_proc_actual->id, p_proc_actual->num_mutex);
	num_mutex_global++;
	printk("----> ACTUALIZACION: MUTEX EN SISTEMA %d\n", num_mutex_global);

	printk("--> MUTEX %s con descriptor %d CREADO: TIENE %d ASOCIADOS \n", mutex->nombre, descriptor_mutex, mutex->n_proc_asociados);
	printk("--> PROCESO TIENE %d mutex\n", p_proc_actual->num_mutex);
	printk("-> PROC %d: FIN CREAR MUTEX %d of 16\n", p_proc_actual->id, num_mutex_global);
	// Devolver descriptor
	return descriptor_mutex;
}

/*
*	Funcion auxiliar para comprobar las condiciones de creación de un mutex
*/
int cumple_requisitos(char *nombre, int descriptor_mutex){
	if(descriptor_mutex == ERROR_MAX_NUM_MUTEX){
		printk("--->ERROR: Se ha alcanzado el maximo numero de mutex\n");
		return ERROR_MAX_NUM_MUTEX;
	}
	// Comprobar que no existe un mutex con ese nombre
	if(descriptor_mutex == ERROR_NOMBRE_REPETIDO){
		printk("--->ERROR: Ya existe un mutex con ese nombre\n");
		return ERROR_NOMBRE_REPETIDO;
	}
	printk("--> COMPROBAMOS REQUISITIOS Y OBTENEMOS %d\n", descriptor_mutex);
	return descriptor_mutex;
}

/**
* 	Funcion auxiliar para esperar hasta que haya hueco en la tabla de mutex
*/
void esperar_hueco_mutex(char* nombre){
	printk("--> PROC %d: ESPERANDO HUECO MUTEX %s\n", p_proc_actual->id, nombre);
	BCPptr p_proc = p_proc_actual;
	p_proc->estado = BLOQUEADO;
	int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
	eliminar_elem(&lista_listos, p_proc);
	insertar_ultimo(&lista_bloq_mutex, p_proc);
	printk("--> PROC %d: INSERTADO EN COLA DE ESPERA DE MUTEX\n", p_proc->id);
	fijar_nivel_int(nivel_interrupcion_previo);
	p_proc_actual = planificador();
	printk("-->C.CONTEXTO POR BLOQUEO de %d a %d\n",p_proc->id, p_proc_actual->id);
	cambio_contexto(&(p_proc->contexto_regs), &(p_proc_actual->contexto_regs));
}

/**
*	Devuelve un descriptor asociado a un mutex ya existente o un número negativo en caso de error
*/
int abrir_mutex(){
	char *nombre = (char *) leer_registro(1);
	printk("-> PROC %d: ABRIR MUTEX %s Nº %d\n", p_proc_actual->id, nombre, p_proc_actual->num_mutex);
	
	// Comprobar que proceso no tiene mas de NUM_MUT_PROC
	if(p_proc_actual->num_mutex >= NUM_MUT_PROC){
		printk("-->ERROR: No tiene hueco para abrir el mutex %s\n", nombre);
		return ERROR_MAX_NUM_MUTEX_PROC;
	}
	// Comprobar que el mutex existe en la tabla de mutex
	int descriptor_mutex = buscar_mutex(nombre);
	printk("---> TRABAJAMOS CON EL DESCRIPTOR %d del MUTEX %s\n", descriptor_mutex, nombre);
	if(descriptor_mutex < 0){
		printk("--->ERROR: El mutex %s no existe\n", nombre);
		return ERROR_MUTEX_NO_EXISTE;
	}

	if(!esta_mutex_asociado_a_proc(descriptor_mutex)){
		// Asociar mutex a proceso
		int index_mutex_proc = obtener_mutex_proc();
		p_proc_actual->descriptores_mutex[index_mutex_proc] = descriptor_mutex;

		// Asociar proceso a mutex
		Mutex *mutex = &tabla_mutex[descriptor_mutex];
		mutex->procs_asociados[mutex->n_proc_asociados] = p_proc_actual->id;

		// Actualizacion de estados
		p_proc_actual->num_mutex++;
		printk("----> ACTUALIZACION: PROC %d TIENE %d MUTEX ASOCIADOS\n", p_proc_actual->id, p_proc_actual->num_mutex);
		mutex->n_proc_asociados++;
		printk("----> ACTUALIZACION: MUTEX %s TIENE %d PROCESOS ASOCIADOS\n", mutex->nombre, mutex->n_proc_asociados);
		printk("--> MUTEX %s con descriptor %d ABIERTO: TIENE %d PROCESOS ASOCIADOS \n", nombre, descriptor_mutex, mutex->n_proc_asociados);
	}

	printk("--> PROCESO TIENE %d mutex\n", p_proc_actual->num_mutex);
	printk("-> PROC %d: FIN ABRIR MUTEX %s. PROCESO TIENE ASOCIADOS %d MUTEXES Y DEVOLVEMOS DESCRIPTOR %d\n", p_proc_actual->id, nombre, p_proc_actual->num_mutex, descriptor_mutex);

	return descriptor_mutex;
}

/*
* 	Funcion auxiliar para ver si el mutex está asociado
* 	Devuelve 1 si lo encuentra || 0 si no lo encuentra
*/
int esta_mutex_asociado_a_proc(int des){
	int encontrado = 0;
	int i = 0;
	while(!encontrado && i < NUM_MUT_PROC){
		if(p_proc_actual->descriptores_mutex[i] == des){
			encontrado = 1;
		}
		i++;
	}
	return encontrado;
}

/**
* 	 Cierra el mutex especificado, devolviendo un número negativo en caso de error.
*/
int cerrar_mutex(){
	unsigned int mutexid = (unsigned int) leer_registro(1);
	Mutex *mutex = &tabla_mutex[mutexid];
	printk("-> PROC %d: CERRAR MUTEX %s\n", p_proc_actual->id, mutex->nombre);
	
	int index_mutex_proc = buscar_descriptor_mutex_proc(mutexid);
	printk("---> TRABAJAMOS CON EL DESCRIPTOR %d del MUTEX %s\n", mutexid, mutex->nombre);

	if(index_mutex_proc < 0){
		printk("--->ERROR: El proceso %d no tiene el mutex %s\n", p_proc_actual->id, mutex->nombre);
		return ERROR_MUTEX_NO_EXISTE;
	}

	// Desasociar proceso al mutex
	int encontrado = 0;
	int index = 0;
	while(index < MAX_PROC && !encontrado){
		if(mutex->procs_asociados[index] == p_proc_actual->id){
			mutex->procs_asociados[index] = NO_USADO;
			encontrado = 1;
		}
		index++;
	}
	mutex->n_proc_asociados--;
	printk("----> ACTUALIZACION: MUTEX %s TIENE %d PROCESOS ASOCIADOS\n", mutex->nombre, mutex->n_proc_asociados);

	// Si proceso tenia bloqueado mutex, desbloquear procesos
	if(mutex->estado == OCUPADO && mutex->id_proceso_lock == p_proc_actual->id){
		printk("----> PROC %d: UNLOCK IMPLICITO\n", p_proc_actual->id);
		mutex->n_veces_lock = 1; // Para que desbloque todos los procesos
		escribir_registro(1,mutexid);
		unlock();
	}


	// Si es el ultimo proceso en estar asociado, eliminamos mutex
	if(mutex->n_proc_asociados == 0){
		mutex->estado = NO_USADO;
		cpy(mutex->nombre, "");
		num_mutex_global--;
		printk("----> ACTUALIZACION: MUTEX EN SISTEMA %d\n", num_mutex_global);

	}

	// Desasociar mutex al proceso
	p_proc_actual->descriptores_mutex[index_mutex_proc] = NO_USADO;
	p_proc_actual->num_mutex--;
	printk("----> ACTUALIZACION: PROC %d TIENE %d MUTEX ASOCIADOS\n", p_proc_actual->id, p_proc_actual->num_mutex);
	
	BCPptr p_proc = lista_bloq_mutex.primero;
	if(p_proc != NULL){
		// Desbloquear procesos bloqueados por el mutex
		printk("---> DESBLOQUEANDO a proc %d por MUTEX %s con descriptor %d\n", p_proc->id, mutex->nombre, mutexid);
		p_proc->estado = LISTO;
		int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
		eliminar_primero(&lista_bloq_mutex);
		insertar_ultimo(&lista_listos, p_proc);
		fijar_nivel_int(nivel_interrupcion_previo);
	}
	
	printk("--> MUTEX %s con descriptor %d CERRADO: TIENE %d PROCESOS ASOCIADOS \n", mutex->nombre, mutexid, mutex->n_proc_asociados);
	printk("--> PROCESO TIENE %d mutex\n", p_proc_actual->num_mutex);
	printk("-> PROC %d: FIN CERRAR MUTEX %s. PROCESO TIENE ASOCIADOS %d MUTEXES Y DEVOLVEMOS DESCRIPTOR %d\n", p_proc_actual->id, mutex->nombre, p_proc_actual->num_mutex, mutexid);
	
	return 0;
}

/**
*	Llamada que intenta bloquear mutex del sistema. 
*	Si el mutex ya está bloqueado por otro proceso, el proceso que realiza la operación se bloquea. 
*	En caso contrario se bloquea el mutex sin bloquear al proceso.
*/
int lock(){
	unsigned int mutexid = (unsigned int) leer_registro(1);
	Mutex *mutex = &tabla_mutex[mutexid];
	printk("-> PROC %d: LOCK MUTEX %s\n", p_proc_actual->id, mutex->nombre);
	
	int index_mutex_proc = buscar_descriptor_mutex_proc(mutexid);
	printk("---> TRABAJAMOS CON EL DESCRIPTOR %d del MUTEX %s\n", mutexid, mutex->nombre);

	if(index_mutex_proc < 0){
		printk("--->ERROR: El proceso %d no tiene el mutex %s\n", p_proc_actual->id, mutex->nombre);
		return ERROR_MUTEX_NO_EXISTE;
	}

	while(mutex->estado == OCUPADO && mutex->id_proceso_lock != p_proc_actual->id){
		// Bloquear proceso en la lista de procesos bloqueados por este mutex
		printk("--> PROC %d: BLOQUEADO POR MUTEX %s\n", p_proc_actual->id, mutex->nombre);
		BCPptr p_proc = p_proc_actual;
		p_proc->estado = BLOQUEADO;
		int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
		eliminar_elem(&lista_listos, p_proc);
		insertar_ultimo(&mutex->procesos_bloqueados, p_proc);
		printk("--> PROC %d: INSERTADO EN COLA DE PROCESOS BLOQUEADOS POR MUTEX %s\n", p_proc->id, mutex->nombre);
		fijar_nivel_int(nivel_interrupcion_previo);
		p_proc_actual = planificador();
		printk("-->C.CONTEXTO POR LOCK de %d a %d\n",p_proc->id, p_proc_actual->id);
		cambio_contexto(&(p_proc->contexto_regs), &(p_proc_actual->contexto_regs));
	}

	// Primera vez que hace lock, asociamos proceso a mutex
	if(mutex->estado == LIBRE){
		printk("----> ACTUALIZACION: PROC %d BLOQUEA MUTEX %s\n", p_proc_actual->id, mutex->nombre);
		mutex->id_proceso_lock = p_proc_actual->id;
	}

	if(mutex->id_proceso_lock == p_proc_actual->id){
		if(mutex->tipo == RECURSIVO){
			mutex->id_proceso_lock = p_proc_actual->id;
			mutex->estado = OCUPADO;
			mutex->n_veces_lock++;
			printk("----> PROC %d: LOCK RECURSIVO N %d del MUTEX %s\n", p_proc_actual->id, mutex->n_veces_lock, mutex->nombre);
		}else{
			// MUTEX NO_RECURSIVO
			if(mutex->n_veces_lock == 0 && mutex->estado == LIBRE){
				mutex->id_proceso_lock = p_proc_actual->id;
				mutex->estado = OCUPADO;
				mutex->n_veces_lock++;
				printk("----> PROC %d: LOCK NO_RECURSIVO N %d del MUTEX %s\n", p_proc_actual->id, mutex->n_veces_lock, mutex->nombre);

			}else{
				printk("--> ERROR: LOCK Nº %d sobre MUTEX %s NO_RECURSIVO\n", mutex->n_veces_lock, mutex->nombre);
				return ERROR_GENERICO;
			}
		}
	}
	return 0;
}

/**
*	Llamada que intenta desbloquearbloquear al sistema. 
*	Si el mutex ya está bloqueado por otro proceso, el proceso que realiza la operación se bloquea. 
*	En caso contrario se bloquea el mutex sin bloquear al proceso.
*/
int unlock(){
	unsigned int mutexid = (unsigned int) leer_registro(1);
	Mutex *mutex = &tabla_mutex[mutexid];
	printk("-> PROC %d: UNLOCK MUTEX %s\n", p_proc_actual->id, mutex->nombre);
	
	int index_mutex_proc = buscar_descriptor_mutex_proc(mutexid);
	printk("---> TRABAJAMOS CON EL DESCRIPTOR %d del MUTEX %s\n", mutexid, mutex->nombre);

	if(index_mutex_proc < 0){
		printk("--->ERROR: El proceso %d no tiene el mutex %s\n", p_proc_actual->id, mutex->nombre);
		return ERROR_MUTEX_NO_EXISTE;
	}

	if(mutex->id_proceso_lock == p_proc_actual->id && mutex->estado == OCUPADO){
		printk("----> PROC %d: TIENE MUTEX %s OCUPADO Y VA A DESBLOQUEARLO\n", p_proc_actual->id, mutex->nombre);
		if(mutex->tipo == RECURSIVO){
			mutex->n_veces_lock--;
			printk("----> PROC %d: UNLOCK RECURSIVO N %d del MUTEX %s\n", p_proc_actual->id, mutex->n_veces_lock, mutex->nombre);
			if(mutex->n_veces_lock == 0){
				printk("----> PROC %d: ULTIMO UNLOCK Y DESBLOQUEA\n", p_proc_actual->id);
				mutex->estado = LIBRE;
				mutex->id_proceso_lock = NO_USADO;
				// Desbloqueamos procesos que estaban bloqueados
				BCP *p_proc = mutex->procesos_bloqueados.primero;
				if(p_proc != NULL){
					printk("----> PROC %d DESBLOQUEA a %d por MUTEX %s\n", p_proc_actual->id, p_proc->id, mutex->nombre);
					p_proc->estado = LISTO;
					int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
					eliminar_elem(&mutex->procesos_bloqueados, p_proc);
					insertar_ultimo(&lista_listos, p_proc);				
					fijar_nivel_int(nivel_interrupcion_previo);
				}
				
			}
		} else{
			// MUTEX NO_RECURSIVO
			if(mutex->n_veces_lock == 1){
				printk("----> PROC %d: UNLOCK Y DESBLOQUEA\n", p_proc_actual->id);
				mutex->n_veces_lock--;
				mutex->estado = LIBRE;
				mutex->id_proceso_lock = NO_USADO;
				// Desbloqueamos procesos que estaban bloqueados
				BCP *p_proc = mutex->procesos_bloqueados.primero;
				if(p_proc != NULL){
					printk("----> PROC %d DESBLOQUEA a %d por MUTEX %s\n", p_proc_actual->id, p_proc->id, mutex->nombre);
					p_proc->estado = LISTO;
					int nivel_interrupcion_previo = fijar_nivel_int(NIVEL_3);
					eliminar_elem(&mutex->procesos_bloqueados, p_proc);
					insertar_ultimo(&lista_listos, p_proc);				
					fijar_nivel_int(nivel_interrupcion_previo);
				}
			}else{
				printk("--> ERROR: UNLOCK ADICIONAL sobre MUTEX %s NO_RECURSIVO\n", mutex->n_veces_lock, mutex->nombre);
				return ERROR_GENERICO;
			}
		}
	} else {
		// Bloquear proceso en la lista de procesos bloqueados por este mutex
		printk("--> ERROR: UNLOCK sobre MUTEX %s QUE NO TENIA PROC %d NO TENIA LOCKED\n", mutex->nombre, p_proc_actual->id);
		return ERROR_GENERICO;
	}
	return 0;
}

// ----------------------------------------------------
// Funciones auxiliares
/**
*	Función auxiliar para obtener longitud de un string
*/
int len(char *string){
	int i = 0;
	while(string[i] != '\0'){
		i++;
	}
	return i;
}

/**
*   Funcion auxiliar para comparar si dos string son iguales
*  	Devuelve 1 si son iguales, 0 en caso contrario
*/
int cmp(char *s1, char *s2){
	int i = 0;
	while(s1[i] != '\0' && s2[i] != '\0'){
		if(s1[i] != s2[i]){
			return 0;
		}
		i++;
	}
	if(s1[i] == '\0' && s2[i] == '\0'){
		return 1;
	}
	return 0;
}

/**
*	Funcion auxiliar para copiar un string en otro
*/
void cpy(char *dest, char *orig){
	int i = 0;
	while(orig[i] != '\0'){
		dest[i] = orig[i];
		i++;
	}
	dest[i] = '\0';
}

/*
 *
 * Rutina de inicializacin invocada en arranque
 *
 */
int main(){
	/* se llega con las interrupciones prohibidas */

	instal_man_int(EXC_ARITM, exc_arit); 
	instal_man_int(EXC_MEM, exc_mem); 
	instal_man_int(INT_RELOJ, int_reloj); 
	instal_man_int(INT_TERMINAL, int_terminal); 
	instal_man_int(LLAM_SIS, tratar_llamsis); 
	instal_man_int(INT_SW, int_sw); 

	iniciar_cont_int();			/* inicia cont. interr. */
	iniciar_cont_reloj(TICK);	/* fija frecuencia del reloj */
	iniciar_cont_teclado();		/* inici cont. teclado */

	iniciar_tabla_proc();		/* inicia BCPs de tabla de procesos */

	iniciar_tabla_mutex();		/* inicia mutex de tabla de mutex */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init")<0)
		panico("no encontrado el proceso inicial");
	
	/* activa proceso inicial */
	p_proc_actual=planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
