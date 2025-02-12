import React from 'react'
import { useState, useEffect, useCallback } from 'react';
import _ from 'underscore';


const Tabs = (props) => {

    const tab_stored = useCallback(() => localStorage.getItem('active-tab') ? 
        localStorage.getItem('active-tab') : props.children[0].props.label, 
    []) //makes this only load from localstorage once

    const [state, setState] = useState({
        activeTab: tab_stored(),
        fetch_in_flight: 0,
        fetch_status: 200,
        fetch_elapsed_ms: 0
    });
    useEffect(() => {
        (async () => {
            try {

                const message_callback = async event => {
                    if (event.data.status == -1) {
                        setState((prevState) => ({ ...prevState, fetch_in_flight: event.data.in_flight}));
                    } else {
                        setState((prevState) => ({ ...prevState, 
                        fetch_in_flight: event.data.in_flight, 
                        fetch_status: event.data.status,
                        fetch_elapsed_ms: event.data.elapsed_ms
                        }));
                    }
                }
                navigator.serviceWorker.removeEventListener("message", message_callback)  //is this necessary?
                navigator.serviceWorker.addEventListener("message", message_callback);
            } catch (error) {
                console.error('Error fetching data: ', error);
            }
        })();
        return () => {
            //cleanup
        }
    }, []);


    const changeTab = (tab) => {
        localStorage.setItem('active-tab', tab)
        setState((prevState) => ({...prevState, activeTab: tab}));
    };
    
    let content;
    const buttons = _.map(props.children, child => {
        if (child.props.label === state.activeTab) {
            content = child.props.children
        }
        return child.props.label
    })

    const circle_color = state.fetch_status == 200 ? 'rgb(25, 132, 28)' : 'rgb(129, 29, 29)'
    const fetch_status = (<div 
        className='input-container flex-row' style={{position: 'absolute', top: '5px', right: '15px', height: '21px'}}>         

        <div className='input-label' style={{fontSize: '0.4em', fontWeight: '700', width: '15px'}}>
            {state.fetch_in_flight}
        </div>
        <div className='input-divider'/>
        <div style={{width: '12px', alignSelf: 'center', marginLeft: '9px', marginRight: '7px'}} >
            <div className='circle' 
                style={{backgroundColor: circle_color}}>
            </div>
        </div>
        <div className='input-divider'/>
        <div className='input-label' style={{fontSize: '0.4em', fontWeight: '700', width: '40px', margin: 'auto'}}>
                {
                    state.fetch_elapsed_ms < 1000 ? `${Math.floor(state.fetch_elapsed_ms)}ms` : 
                    `${(Math.floor(state.fetch_elapsed_ms / 100))/10}s`
                }
        </div>
    
    </div>)

    return (
        <div className='tab-container'>
            <div className='tab-bar'>
               <h2 style={{position: 'relative'}}> {props.title} {fetch_status} </h2> 
                <TabButtons activeTab={state.activeTab} buttons={buttons} changeTab={changeTab}/>
            </div>
            <div className='tab-content'>
                {content}
            </div>     
        </div>
        );
}

const TabButtons = ({buttons, changeTab, activeTab}) =>{
    
    return <div className='flex-row'>
        {_.map(buttons, (button, i) => 
            <div key={i} className={`${button === activeTab? 'active-tab-button ':''}tab-button`} 
            onClick={()=>changeTab(button)}>{button}</div>
        )}
    </div>
    
}

const Tab = props =>{
    return(
        <React.Fragment>
        {props.children}
        </React.Fragment>
    )
}
export {Tabs, Tab}
