import React from 'react';
import { useState, useEffect, useCallback } from 'react'
import _ from 'underscore';

import Utilities from './utilities'

const Peers = (props) => {

    const known_peers_stored = useCallback(
        () => _.size(localStorage.getItem('known_peers')) > 0 ? 
            JSON.parse(localStorage.getItem('known_peers')) : {'moxbox.local': 'origin'}, 
    []) //makes this only load from localstorage once
    
    const [state, setState] = useState({
        peers: null, 
        known_peers: known_peers_stored(),
        peer_manual_add: '',
        peer_to_delete: ''
        
    })
    const util = Utilities(state, setState, props.server_ip);
   
    useEffect(() => {
        (async () => {
            try {
                await setState((prevState) => ({ ...prevState, peers: []}));
                await util.device_to_state('peers')
            } catch (error) {  
            }
        })();
        return () => {
        }
    }, []);

    
    let peer_change = (ip, field) => (event) => {
        if (field == 'selected' && event.target.checked) {
            props.set_server_ip(ip)
        } else if (field == 'name') {
            const known_peers = {...state.known_peers, [ip]: event.target.value}
            setState((prevState) => ({ ...prevState, known_peers }));
            localStorage.setItem('known_peers', JSON.stringify(known_peers));
        }
    }

    let known_peers_render = (_.size(state.known_peers) > 0) ? _.map(state.known_peers, (name, ip) => {
        return <div className='flex-row input-container' key={ip}>
            <input value={name} type={'text'} className='dark' onChange={peer_change(ip, 'name')}/>
            <div  className='input-divider' />     
            <div className='input-label label-narrow light'> {ip} </div>     
            <div  className='input-divider' />            
            <label className='input-label flex-row dark checkbox-container'>
                <div className='checkbox-box'>
                    <input  type='checkbox' checked={props.server_ip == ip} onChange={peer_change(ip, 'selected')}/> 
                </div>
            </label>
        
        </div>}) : <div/>

    let save_peer = (ip, name) => () => {
        const known_peers = {...state.known_peers, [ip]: name}
        setState((prevState) => ({ ...prevState, known_peers, peer_manual_add: ''}));
        localStorage.setItem('known_peers', JSON.stringify(known_peers));
    }
    let delete_peer = (ip) => () => {
        const known_peers = _.omit(state.known_peers, ip)
        setState((prevState) => ({ ...prevState, known_peers, peer_to_delete: '' }));
        localStorage.setItem('known_peers', JSON.stringify(known_peers));

    }
    let textbox_change = (key) => (event) => {
        setState((prevState) => ({ ...prevState, [key]: event.target.value }));
    }
    let refresh_peers = () => {
        setState((prevState) => ({ ...prevState, peers: []}));
        util.device_to_state('peers')
    }
    let new_peers_render
    if (_.size(state.peers) == 0) {
        new_peers_render = <span> Looking for new peers...</span> 
    } else {
        let new_peers =  _.filter(state.peers, (peer) => !_.contains(_.keys(state.known_peers), peer.addr[0].ip))
        if (new_peers.length > 0) {
            new_peers_render =  _.map(new_peers, (peer, i) => {
                return <div className='flex-row input-container' key={i}>
                        <span className='input-label label-narrow dark' > {peer.name} </span>
                        <div  className='input-divider' />    
                        <div className='input-label light' > {peer.addr[0].ip} </div>     
                        <div  className='input-divider' />          
                        <button className='input-container dark' onClick={save_peer(peer.addr[0].ip, peer.name)}
                            style={{borderTopLeftRadius: '0px', borderBottomLeftRadius: '0px', width: '75px'}} 
                        > 
                            Add 
                        </button> 
                    </div>
            })
        } else {
            new_peers_render = <span> No new peers </span> 
        }
    }
        
        
    return [
        <div className='pane top-pane' key='1'>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-left'>
                <h4> Known </h4>
            </div>
            <div style={{height: '10px'}}/>  
            <div className='flex-row wrap-container justify-left'>
                {known_peers_render}
            </div>
            <div style={{height: '20px'}}/>      
        </div>,
        <div className='pane bottom-pane' key='2'>
            <div style={{height: '15px'}}/>
            <div className='flex-row wrap-container justify-left'>
                <h4> Discovered </h4>
            </div>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-right'>
                {new_peers_render}
            </div>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-right'>
                <button className='input-container dark' onClick={refresh_peers}>
                    Refresh
                </button>
            </div>

            <div style={{height: '20px'}}/>
            <div className='flex-row wrap-container justify-left'>
                <h4> Add Other </h4>
            </div>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-right'>
                <div className='flex-row input-container'>
                    <span className='input-label label-narrow light' > Enter ip/host: </span>    
                    <div  className='input-divider' />                   
                    <input value={state.peer_manual_add} type={'text'} className='dark' onChange={textbox_change('peer_manual_add')}/>
                    <div  className='input-divider' />          
                    <button className='dark' onClick={save_peer(state.peer_manual_add, state.peer_manual_add)} style={{width: '75px'}}> Add </button> 
                </div>
            </div>
            <div style={{height: '20px'}}/>
            <div className='flex-row wrap-container justify-left'>
                <h4> Delete Peer </h4>
            </div>
            <div style={{height: '10px'}}/>
            <div className='flex-row wrap-container justify-right'>
                <div className='flex-row input-container'>
                    <span className='input-label label-narrow light' > Enter ip/host: </span>    
                    <div  className='input-divider' />                   
                    <input value={state.peer_to_delete} type={'text'} className='dark' onChange={textbox_change('peer_to_delete')}/>
                    <div  className='input-divider' />          
                    <button className='dark' onClick={delete_peer(state.peer_to_delete)} style={{width: '75px'}}> Delete </button> 
                </div>
            </div>
            <div style={{height: '20px'}}/>
        </div>
    ]
  
};

export default Peers;