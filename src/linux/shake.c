/* libShake - a basic haptic library */

#define _GNU_SOURCE

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#include "shake.h"
#include "shake_private.h"
#include "../common/helpers.h"

#define SHAKE_TEST(x) ((x) ? SHAKE_TRUE : SHAKE_FALSE)

listElement *listHead;
unsigned int numOfDevices;

/* Prototypes */

int Shake_Probe(Shake_Device *dev);
int Shake_Query(Shake_Device *dev);

int nameFilter(const struct dirent *entry)
{
	const char filter[] = "event";
	return !strncmp(filter, entry->d_name, strlen(filter));
}

/* Public functions */

Shake_Status Shake_Init()
{
	struct dirent **nameList;
	int numOfEntries;

	numOfDevices = 0;

	numOfEntries = scandir(SHAKE_DIR_NODES, &nameList, nameFilter, alphasort);

	if (numOfEntries < 0)
	{
		perror("Shake_Init: Failed to retrieve device nodes.");
		return SHAKE_ERROR;
	}
	else
	{
		int i;

		for (i = 0; i < numOfEntries; ++i)
		{
			Shake_Device dev;

			dev.node = malloc(strlen(SHAKE_DIR_NODES) + strlen("/") + strlen(nameList[i]->d_name) + 1);
			if (dev.node == NULL)
			{
				return SHAKE_ERROR;
			}

			strcpy(dev.node, SHAKE_DIR_NODES);
			strcat(dev.node, "/");
			strcat(dev.node, nameList[i]->d_name);

			if (Shake_Probe(&dev) == SHAKE_OK)
			{
				dev.id = numOfDevices;
				listHead = listElementPrepend(listHead);
				listHead->item = malloc(sizeof(Shake_Device));
				memcpy(listHead->item, &dev, sizeof(Shake_Device));
				++numOfDevices;
			}

			free(nameList[i]);
		}

		free(nameList);
	}

	return SHAKE_OK;
}

void Shake_Quit()
{
	if (listHead != NULL)
	{
		listElement *curElem = listHead;

		while(curElem)
		{
			Shake_Device *dev;
			listElement *toDelElem = curElem;
			curElem = curElem->next;
			dev = (Shake_Device *)toDelElem->item;

			Shake_Close(dev);
			if (dev->node != NULL)
				free(dev->node);
		}

		listElementDeleteAll(listHead);
	}
}

int Shake_NumOfDevices()
{
	return numOfDevices;
}

Shake_Status Shake_Probe(Shake_Device *dev)
{
	int isHaptic;

	if(!dev || !dev->node)
		return SHAKE_ERROR;

	dev->fd = open(dev->node, O_RDWR);

	if (!dev->fd)
		return SHAKE_ERROR;

	isHaptic = !Shake_Query(dev);
	dev->fd = close(dev->fd);
	
	return isHaptic ? SHAKE_OK : SHAKE_ERROR;
}

Shake_Device *Shake_Open(unsigned int id)
{
	Shake_Device *dev;

	if (id >= numOfDevices)
		return NULL;

	listElement *element = listElementGet(listHead, numOfDevices - 1 - id);
	dev = (Shake_Device *)element->item;

	if(!dev || !dev->node)
		return NULL;

	dev->fd = open(dev->node, O_RDWR);

	return dev->fd ? dev : NULL;
}

Shake_Status Shake_Query(Shake_Device *dev)
{
	int size = sizeof(dev->features)/sizeof(unsigned long);
	int i;

	if(!dev)
		return SHAKE_ERROR;

	if (ioctl(dev->fd, EVIOCGBIT(EV_FF, sizeof(dev->features)), dev->features) == -1)
	{
		perror("Shake_Query: Failed to query for device features.");
		return SHAKE_ERROR;
	}

	for (i = 0; i < size; ++i)
	{
		if (dev->features[i])
			break;
	}

	if (i >= size) /* Device doesn't support any force feedback effects. Ignore it. */
		return SHAKE_ERROR;

	if (ioctl(dev->fd, EVIOCGEFFECTS, &dev->capacity) == -1)
	{
		perror("Shake_Query: Failed to query for device effect capacity.");
		return SHAKE_ERROR;
	}

	if (dev->capacity <= 0) /* Device doesn't support uploading effects. Ignore it. */
		return SHAKE_ERROR;

	if (ioctl(dev->fd, EVIOCGNAME(sizeof(dev->name)), dev->name) == -1) /* Get device name */
	{
		strncpy(dev->name, "Unknown", sizeof(dev->name));
	}

	return SHAKE_OK;
}

int Shake_DeviceId(Shake_Device *dev)
{
	return dev ? dev->id : SHAKE_ERROR;
}

const char *Shake_DeviceName(Shake_Device *dev)
{
	return dev ? dev->name : NULL;
}

int Shake_DeviceEffectCapacity(Shake_Device *dev)
{
	return dev ? dev->capacity : SHAKE_ERROR;
}

Shake_Bool Shake_QueryEffectSupport(Shake_Device *dev, Shake_EffectType type)
{
	/* Starts at a magic, non-zero number, FF_RUMBLE.
	   Increments respectively to EffectType. */
	return SHAKE_TEST(test_bit(FF_RUMBLE + type, dev->features));
}

Shake_Bool Shake_QueryWaveformSupport(Shake_Device *dev, Shake_PeriodicWaveform waveform)
{
	/* Starts at a magic, non-zero number, FF_SQUARE.
	   Increments respectively to PeriodicWaveform. */
	return SHAKE_TEST(test_bit(FF_SQUARE + waveform, dev->features));
}

Shake_Bool Shake_QueryGainSupport(Shake_Device *dev)
{
	return SHAKE_TEST(test_bit(FF_GAIN, dev->features));
}

Shake_Bool Shake_QueryAutocenterSupport(Shake_Device *dev)
{
	return SHAKE_TEST(test_bit(FF_AUTOCENTER, dev->features));
}

Shake_Status Shake_SetGain(Shake_Device *dev, int gain)
{
	struct input_event ie;

	if (!dev)
	{
		return SHAKE_ERROR;
	}

	if (gain < 0)
		gain = 0;
	if (gain > 100)
		gain = 100;

	ie.type = EV_FF;
	ie.code = FF_GAIN;
	ie.value = 0xFFFFUL * gain / 100;

	if (write(dev->fd, &ie, sizeof(ie)) == -1)
	{
		perror("Shake_SetGain: Failed to set gain.");
		return SHAKE_ERROR;
	}

	return SHAKE_OK;
}

Shake_Status Shake_SetAutocenter(Shake_Device *dev, int autocenter)
{
	struct input_event ie;

	if (!dev)
	{
		return SHAKE_ERROR;
	}

	if (autocenter < 0)
		autocenter = 0;
	if (autocenter > 100)
		autocenter = 100;

	ie.type = EV_FF;
	ie.code = FF_AUTOCENTER;
	ie.value = 0xFFFFUL * autocenter / 100;

	if (write(dev->fd, &ie, sizeof(ie)) == -1)
	{
		perror("Shake_SetAutocenter: Failed to set auto-center.");
		return SHAKE_ERROR;
	}

	return SHAKE_OK;
}

Shake_Status Shake_InitEffect(Shake_Effect *effect, Shake_EffectType type)
{
	if (!effect)
	{
		return SHAKE_ERROR;
	}

	memset(effect, 0, sizeof(*effect));
	if (type < 0 || type >= SHAKE_EFFECT_COUNT)
	{
		perror("Shake_InitEffect: Unsupported effect.");
		return SHAKE_ERROR;
	}
	effect->type = type;
	effect->id = -1;

	return SHAKE_OK;
}

int Shake_UploadEffect(Shake_Device *dev, Shake_Effect *effect)
{
	struct ff_effect e;

	if (!dev)
	{
		return SHAKE_ERROR;
	}
	if (!effect)
	{
		return SHAKE_ERROR;
	}
	if (effect->id < -1) {
		return SHAKE_ERROR;
	}

	if(effect->type == SHAKE_EFFECT_RUMBLE)
	{
		e.type = FF_RUMBLE;
		e.id = effect->id;
		e.u.rumble.strong_magnitude = effect->u.rumble.strongMagnitude;
		e.u.rumble.weak_magnitude = effect->u.rumble.weakMagnitude;
		e.replay.delay = effect->delay;
		e.replay.length = effect->length;
	}
	else if(effect->type == SHAKE_EFFECT_PERIODIC)
	{
		e.type = FF_PERIODIC;
		e.id = effect->id;
		e.u.periodic.waveform = FF_SQUARE + effect->u.periodic.waveform;
		e.u.periodic.period = effect->u.periodic.period;
		e.u.periodic.magnitude = effect->u.periodic.magnitude;
		e.u.periodic.offset = effect->u.periodic.offset;
		e.u.periodic.phase = effect->u.periodic.phase;
		e.u.periodic.envelope.attack_length = effect->u.periodic.envelope.attackLength;
		e.u.periodic.envelope.attack_level = effect->u.periodic.envelope.attackLevel;
		e.u.periodic.envelope.fade_length = effect->u.periodic.envelope.fadeLength;
		e.u.periodic.envelope.fade_level = effect->u.periodic.envelope.fadeLevel;
		e.trigger.button = 0;
		e.trigger.interval = 0;
		e.direction = effect->direction;
		e.replay.delay = effect->delay;
		e.replay.length = effect->length;
	}
	else if(effect->type == SHAKE_EFFECT_CONSTANT)
	{
		e.type = FF_CONSTANT;
		e.id = effect->id;
		e.u.constant.level = effect->u.constant.level;
		e.u.constant.envelope.attack_length = effect->u.constant.envelope.attackLength;
		e.u.constant.envelope.attack_level = effect->u.constant.envelope.attackLevel;
		e.u.constant.envelope.fade_length = effect->u.constant.envelope.fadeLength;
		e.u.constant.envelope.fade_level = effect->u.constant.envelope.fadeLevel;
		e.trigger.button = 0;
		e.trigger.interval = 0;
		e.replay.delay = effect->delay;
		e.replay.length = effect->length;
	}
	else if(effect->type == SHAKE_EFFECT_RAMP)
	{
		e.type = FF_RAMP;
		e.id = effect->id;
		e.u.ramp.start_level = effect->u.ramp.startLevel;
		e.u.ramp.end_level = effect->u.ramp.endLevel;
		e.u.ramp.envelope.attack_length = effect->u.ramp.envelope.attackLength;
		e.u.ramp.envelope.attack_level = effect->u.ramp.envelope.attackLevel;
		e.u.ramp.envelope.fade_length = effect->u.ramp.envelope.fadeLength;
		e.u.ramp.envelope.fade_level = effect->u.ramp.envelope.fadeLevel;
		e.trigger.button = 0;
		e.trigger.interval = 0;
		e.replay.delay = effect->delay;
		e.replay.length = effect->length;
	}
	else
	{
		perror("Shake_UploadEffect: Unsupported effect.");
		return SHAKE_ERROR;
	}

	if (ioctl(dev->fd, EVIOCSFF, &e) == -1)
	{
		perror("Shake_UploadEffect: Failed to upload effect.");
		return SHAKE_ERROR;
	}

	return e.id;
}

Shake_Status Shake_EraseEffect(Shake_Device *dev, int id)
{
	if (!dev)
	{
		return SHAKE_ERROR;
	}

	if (id < 0)
	{
		return SHAKE_ERROR;
	}

	if (ioctl(dev->fd, EVIOCRMFF, id) == -1)
	{
		perror("Shake_EraseEffect: Failed to erase effect.");
		return SHAKE_ERROR;
	}

  return SHAKE_OK;
}

Shake_Status Shake_Play(Shake_Device *dev, int id)
{
	if(!dev)
	{
		return SHAKE_ERROR;
	}
	if(id < 0)
	{
		return SHAKE_ERROR;
	}

	struct input_event play;
	play.type = EV_FF;
	play.code = id; /* the id we got when uploading the effect */
	play.value = FF_STATUS_PLAYING; /* play: FF_STATUS_PLAYING, stop: FF_STATUS_STOPPED */

	if (write(dev->fd, (const void*) &play, sizeof(play)) == -1)
	{
		perror("Shake_Play: Failed to send play event.");
		return SHAKE_ERROR;
	}

	return SHAKE_OK;
}

Shake_Status Shake_Stop(Shake_Device *dev, int id)
{
	if(!dev)
	{
		return SHAKE_ERROR;
	}
	if(id < 0)
	{
		return SHAKE_ERROR;
	}

	struct input_event stop;
	stop.type = EV_FF;
	stop.code = id; /* the id we got when uploading the effect */
	stop.value = FF_STATUS_STOPPED;

	if (write(dev->fd, (const void*) &stop, sizeof(stop)) == -1)
	{
		perror("Shake_Stop: Failed to send stop event.");
		return SHAKE_ERROR;
	}

	return SHAKE_OK;
}

Shake_Status Shake_Close(Shake_Device *dev)
{
	if (!dev)
	{
		return SHAKE_ERROR;
	}

	close(dev->fd);

	return SHAKE_OK;
}
