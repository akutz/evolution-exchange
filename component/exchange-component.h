/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __EXCHANGE_COMPONENT_H__
#define __EXCHANGE_COMPONENT_H__

#include <bonobo/bonobo-object.h>
#include <shell/Evolution.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_COMPONENT               (exchange_component_get_type ())
#define EXCHANGE_COMPONENT(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_COMPONENT, ExchangeComponent))
#define EXCHANGE_COMPONENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_COMPONENT, ExchangeComponentClass))
#define EXCHANGE_IS_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_COMPONENT))
#define EXCHANGE_IS_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCHANGE_TYPE_COMPONENT))

typedef struct ExchangeComponent                ExchangeComponent;
typedef struct ExchangeComponentPrivate         ExchangeComponentPrivate;
typedef struct ExchangeComponentClass           ExchangeComponentClass;

struct ExchangeComponent {
	BonoboObject parent;
	
	ExchangeComponentPrivate *priv;
};

struct ExchangeComponentClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Component__epv epv;
};


GType              exchange_component_get_type (void);
ExchangeComponent *exchange_component_peek     (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_COMPONENT_H__ */
